#include "actualizar.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <map>
#include <mutex>
#include <sstream>

#include "red.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

const wchar_t* HOST = L"kcinick.gxzone.com";
const wchar_t* BASE = L"/kciwapp/";
const wchar_t* MANIFIESTO = L"kciwapp.md5";

std::mutex g_mu;
actualizar::Estado g_estado;

template <class F>
void con_estado(F f) {
    std::lock_guard<std::mutex> l(g_mu);
    f(g_estado);
}

// ---- md5 --------------------------------------------------------------------

std::string hex(const unsigned char* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

// md5 de un archivo, leido de a 1 MB. Vacio si no se pudo abrir.
std::string md5_de(const std::wstring& ruta) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hh = nullptr;
    std::string r;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0) {
        std::string buf(1 << 20, '\0');
        DWORD leido = 0;
        bool ok = true;
        while (ReadFile(h, buf.data(), (DWORD)buf.size(), &leido, nullptr) && leido > 0)
            if (BCryptHashData(hh, (PUCHAR)buf.data(), leido, 0) != 0) {
                ok = false;
                break;
            }
        unsigned char d[16];
        if (ok && BCryptFinishHash(hh, d, sizeof d, 0) == 0) r = hex(d, 16);
    }
    if (hh) BCryptDestroyHash(hh);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return r;
}

// La cache de md5 locales (datos\md5.txt: "md5 tamano mtime ruta"), para no
// releer 2,7 GB de whisper en cada chequeo.
struct Cacheado {
    std::string md5;
    long long tamano = 0, mtime = 0;
};

std::wstring ruta_cache(const std::wstring& carpeta) { return carpeta + L"\\datos\\md5.txt"; }

std::map<std::wstring, Cacheado> leer_cache(const std::wstring& carpeta) {
    std::map<std::wstring, Cacheado> m;
    HANDLE h = CreateFileW(ruta_cache(carpeta).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return m;
    std::string s;
    char buf[4096];
    DWORD leido = 0;
    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) s.append(buf, leido);
    CloseHandle(h);
    std::istringstream in(s);
    std::string linea;
    while (std::getline(in, linea)) {
        std::istringstream l(linea);
        Cacheado c;
        std::string ruta;
        if (!(l >> c.md5 >> c.tamano >> c.mtime)) continue;
        std::getline(l, ruta);
        while (!ruta.empty() && ruta.front() == ' ') ruta.erase(ruta.begin());
        while (!ruta.empty() && (ruta.back() == '\r' || ruta.back() == ' ')) ruta.pop_back();
        if (!ruta.empty()) m[ancho(ruta)] = c;
    }
    return m;
}

void guardar_cache(const std::wstring& carpeta, const std::map<std::wstring, Cacheado>& m) {
    CreateDirectoryW((carpeta + L"\\datos").c_str(), nullptr);
    std::string s;
    for (auto& [ruta, c] : m) s += c.md5 + " " + std::to_string(c.tamano) + " " + std::to_string(c.mtime) + " " + angosto(ruta) + "\n";
    HANDLE h = CreateFileW(ruta_cache(carpeta).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD e = 0;
    WriteFile(h, s.data(), (DWORD)s.size(), &e, nullptr);
    CloseHandle(h);
}

bool datos_de(const std::wstring& ruta, long long& tamano, long long& mtime) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(ruta.c_str(), GetFileExInfoStandard, &a)) return false;
    tamano = ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    mtime = ((long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    return true;
}

// ---- https ------------------------------------------------------------------

// GET https://HOST<ruta>. Con destino, escribe ahi de a pedazos y avisa los
// bytes; sin destino, devuelve el cuerpo. Devuelve false si fallo.
bool bajar_https(const std::wstring& ruta, std::string* cuerpo, const std::wstring& destino, const std::function<void(long long)>& avance) {
    HINTERNET s = WinHttpOpen(L"kciwapp2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 60000, 60000);
    bool ok = false;
    HINTERNET con = WinHttpConnect(s, HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", ruta.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    HANDLE f = INVALID_HANDLE_VALUE;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD estado = 0, largo = sizeof estado;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &estado, &largo, WINHTTP_NO_HEADER_INDEX);
        if (estado == 200) {
            ok = true;
            if (!destino.empty()) {
                f = CreateFileW(destino.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                ok = f != INVALID_HANDLE_VALUE;
            }
            std::string buf;
            long long total = 0;
            while (ok) {
                DWORD disponible = 0;
                if (!WinHttpQueryDataAvailable(req, &disponible)) {
                    ok = false;
                    break;
                }
                if (disponible == 0) break;
                buf.resize(disponible);
                DWORD leido = 0;
                if (!WinHttpReadData(req, buf.data(), disponible, &leido)) {
                    ok = false;
                    break;
                }
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD e = 0;
                    if (!WriteFile(f, buf.data(), leido, &e, nullptr) || e != leido) ok = false;
                } else if (cuerpo) {
                    cuerpo->append(buf.data(), leido);
                }
                total += leido;
                if (avance) avance(total);
            }
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(s);
    return ok;
}

// La ruta web de un archivo: / en vez de \ y espacios escapados.
std::wstring ruta_web(const std::wstring& rel) {
    std::wstring r = BASE;
    for (wchar_t c : rel) {
        if (c == L'\\') r += L'/';
        else if (c == L' ') r += L"%20";
        else r += c;
    }
    return r;
}

// Parsea el manifiesto: "md5 tamano ruta" por linea (ruta con /).
std::vector<actualizar::Archivo> parsear_manifiesto(const std::string& s) {
    std::vector<actualizar::Archivo> lista;
    std::istringstream in(s);
    std::string linea;
    while (std::getline(in, linea)) {
        if (linea.empty() || linea[0] == '#') continue;
        std::istringstream l(linea);
        actualizar::Archivo a;
        std::string ruta;
        if (!(l >> a.md5 >> a.tamano)) continue;
        std::getline(l, ruta);
        while (!ruta.empty() && ruta.front() == ' ') ruta.erase(ruta.begin());
        while (!ruta.empty() && (ruta.back() == '\r' || ruta.back() == ' ')) ruta.pop_back();
        if (ruta.empty() || a.md5.size() != 32) continue;
        for (auto& c : ruta)
            if (c == '/') c = '\\';
        a.ruta = ancho(ruta);
        lista.push_back(a);
    }
    return lista;
}

// Borra *.viejo y *.nuevo debajo de una carpeta, recursivo.
void borrar_restos(const std::wstring& carpeta) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((carpeta + L"\\*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = d.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring ruta = carpeta + L"\\" + n;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (n != L"datos") borrar_restos(ruta);
        } else if (n.size() > 6 && (n.compare(n.size() - 6, 6, L".viejo") == 0 || n.compare(n.size() - 6, 6, L".nuevo") == 0)) {
            DeleteFileW(ruta.c_str());
        }
    } while (FindNextFileW(h, &d));
    FindClose(h);
}

}  // namespace

namespace actualizar {

Estado estado() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_estado;
}

void verificar(const std::wstring& carpeta_exe, std::function<void()> al_terminar) {
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_estado.verificando || g_estado.bajando) return;
        g_estado.verificando = true;
        g_estado.error.clear();
    }
    red::en_fondo([carpeta_exe, al_terminar] {
        std::string cuerpo;
        std::vector<Archivo> pendientes;
        std::wstring error;
        if (!bajar_https(std::wstring(BASE) + MANIFIESTO, &cuerpo, L"", nullptr)) {
            error = L"Could not reach the update server";
        } else {
            auto lista = parsear_manifiesto(cuerpo);
            if (lista.empty()) error = L"Bad update manifest";
            auto cache = leer_cache(carpeta_exe);
            bool cache_cambio = false;
            for (const Archivo& a : lista) {
                std::wstring ruta = carpeta_exe + L"\\" + a.ruta;
                long long tamano = 0, mtime = 0;
                if (!datos_de(ruta, tamano, mtime)) {
                    pendientes.push_back(a);
                    continue;
                }
                auto it = cache.find(a.ruta);
                std::string md5;
                if (it != cache.end() && it->second.tamano == tamano && it->second.mtime == mtime) {
                    md5 = it->second.md5;
                } else {
                    md5 = md5_de(ruta);
                    cache[a.ruta] = {md5, tamano, mtime};
                    cache_cambio = true;
                }
                if (md5 != a.md5) pendientes.push_back(a);
            }
            if (cache_cambio) guardar_cache(carpeta_exe, cache);
        }
        long long total = 0;
        for (auto& a : pendientes) total += a.tamano;
        red::registrar("actualizar: " + std::to_string(pendientes.size()) + " archivos distintos" + (error.empty() ? "" : " (" + angosto(error) + ")"));
        con_estado([&](Estado& e) {
            e.verificando = false;
            e.error = error;
            e.pendientes = pendientes;
            e.hay = !pendientes.empty() && error.empty();
            e.total_bytes = total;
            e.bajados = 0;
            e.hechos = 0;
        });
        red::en_ui(al_terminar);
    });
}

void bajar(const std::wstring& carpeta_exe, std::function<void()> progreso, std::function<void(bool)> al_terminar) {
    std::vector<Archivo> lista;
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_estado.bajando || g_estado.verificando || !g_estado.hay) return;
        g_estado.bajando = true;
        g_estado.bajados = 0;
        g_estado.hechos = 0;
        g_estado.error.clear();
        lista = g_estado.pendientes;
    }
    red::en_fondo([carpeta_exe, lista, progreso, al_terminar] {
        bool ok = true;
        long long acumulado = 0;
        for (const Archivo& a : lista) {
            std::wstring destino = carpeta_exe + L"\\" + a.ruta + L".nuevo";
            size_t corte = destino.find_last_of(L'\\');
            // Las carpetas intermedias (una nueva en el paquete).
            for (size_t i = carpeta_exe.size() + 1; i < corte; i++)
                if (destino[i] == L'\\') CreateDirectoryW(destino.substr(0, i).c_str(), nullptr);
            con_estado([&](Estado& e) { e.actual = a.ruta; });
            ULONGLONG ultimo_aviso = 0;
            bool bien = bajar_https(ruta_web(a.ruta), nullptr, destino, [&](long long bytes) {
                con_estado([&](Estado& e) { e.bajados = acumulado + bytes; });
                ULONGLONG t = GetTickCount64();
                if (t - ultimo_aviso > 100) {
                    ultimo_aviso = t;
                    red::en_ui(progreso);
                }
            });
            if (bien && md5_de(destino) != a.md5) {
                red::registrar("actualizar: md5 distinto al bajar " + angosto(a.ruta));
                bien = false;
            }
            if (!bien) {
                DeleteFileW(destino.c_str());
                ok = false;
                con_estado([&](Estado& e) { e.error = L"Download failed: " + a.ruta; });
                break;
            }
            acumulado += a.tamano;
            con_estado([&](Estado& e) {
                e.bajados = acumulado;
                e.hechos++;
            });
            red::en_ui(progreso);
        }
        con_estado([&](Estado& e) { e.bajando = false; });
        red::en_ui([al_terminar, ok] { al_terminar(ok); });
    });
}

bool aplicar(const std::wstring& carpeta_exe) {
    std::vector<Archivo> lista;
    {
        std::lock_guard<std::mutex> l(g_mu);
        lista = g_estado.pendientes;
    }
    bool ok = true;
    for (const Archivo& a : lista) {
        std::wstring ruta = carpeta_exe + L"\\" + a.ruta, nuevo = ruta + L".nuevo", viejo = ruta + L".viejo";
        if (GetFileAttributesW(nuevo.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        DeleteFileW(viejo.c_str());
        if (GetFileAttributesW(ruta.c_str()) != INVALID_FILE_ATTRIBUTES && !MoveFileExW(ruta.c_str(), viejo.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            red::registrar("actualizar: no se pudo apartar " + angosto(a.ruta) + " (" + std::to_string(GetLastError()) + ")");
            ok = false;
            continue;
        }
        if (!MoveFileExW(nuevo.c_str(), ruta.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            red::registrar("actualizar: no se pudo poner " + angosto(a.ruta) + " (" + std::to_string(GetLastError()) + ")");
            MoveFileExW(viejo.c_str(), ruta.c_str(), MOVEFILE_REPLACE_EXISTING);
            ok = false;
        }
    }
    red::registrar(std::string("actualizar: aplicado ") + (ok ? "bien" : "con errores"));
    con_estado([&](Estado& e) {
        e.hay = false;
        e.pendientes.clear();
    });
    return ok;
}

void limpiar(const std::wstring& carpeta_exe) { borrar_restos(carpeta_exe); }

}  // namespace actualizar
