// El instalador de kciwapp: un exe chico, propio (GDI, sin dialogos de
// Windows salvo el de elegir carpeta), que pregunta donde instalar, baja todo
// del release "current" de GitHub segun el manifiesto kciwapp.md5
// (verificando cada md5), deja el manifiesto al lado del exe (para el
// "check for updates" del cliente), crea los accesos directos y abre kciwapp.
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winhttp.h>

#include <atomic>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

const wchar_t* URL_MANIFIESTO = L"https://github.com/kcinickgx/kciwapp/releases/download/current/kciwapp.md5";
const wchar_t* MANIFIESTO = L"kciwapp.md5";
const UINT WM_AVANCE = WM_APP + 1;  // el hilo de bajada avisa (wp: 0 progreso, 1 listo, 2 error)

// Colores (COLORREF es BGR).
const COLORREF BG = RGB(0x11, 0x1b, 0x21), PANEL = RGB(0x20, 0x2c, 0x33), CAMPO = RGB(0x2a, 0x39, 0x42);
const COLORREF ACCENT = RGB(0x00, 0xa8, 0x84), TXT = RGB(0xe9, 0xed, 0xef), DIM = RGB(0x86, 0x96, 0xa0), ROJO = RGB(0xf1, 0x5c, 0x6d);

struct Archivo {
    std::wstring ruta;  // relativa, con \ (mpv\libmpv-2.dll)
    std::string md5;
    long long tamano = 0;
    std::wstring url;   // completa
};

// ---- estado -----------------------------------------------------------------

enum Paso { ELEGIR, BAJANDO, LISTO, ERROR_ };

HWND g_hwnd;
Paso g_paso = ELEGIR;
std::wstring g_carpeta;
bool g_acceso_escritorio = true;
bool g_whisper = true;  // la transcripcion de notas de voz (whisper\, 2,7 GB)
bool g_traductor = true;  // la traduccion de mensajes (translate\, 5,3 GB)
std::atomic<bool> g_cancelar{false};
// Progreso (lo escribe el hilo, lo lee la ventana; con el mutex simple de abajo).
CRITICAL_SECTION g_cs;
std::wstring g_actual, g_error;
long long g_total = 0, g_bajado = 0;
int g_hechos = 0, g_cuantos = 0;
float g_escala = 1.0f;
bool g_mano = false;

int E(float px) { return (int)(px * g_escala + 0.5f); }

// ---- utiles -----------------------------------------------------------------

std::wstring ancho(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}

std::wstring megas(long long b) {
    wchar_t t[32];
    if (b >= 100 * 1024 * 1024) swprintf(t, 32, L"%.0f MB", b / 1048576.0);
    else if (b >= 1024 * 1024) swprintf(t, 32, L"%.1f MB", b / 1048576.0);
    else swprintf(t, 32, L"%.0f KB", b / 1024.0);
    return t;
}

std::string hex(const unsigned char* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

// md5 incremental: se alimenta a medida que baja.
struct Md5 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    Md5() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0) BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    }
    void meter(const char* d, size_t n) {
        if (h) BCryptHashData(h, (PUCHAR)d, (ULONG)n, 0);
    }
    std::string terminar() {
        unsigned char d[16] = {};
        if (h) BCryptFinishHash(h, d, 16, 0);
        return hex(d, 16);
    }
    ~Md5() {
        if (h) BCryptDestroyHash(h);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
};

// GET de una URL https completa (siguiendo redirecciones al CDN): cada
// pedazo va a recibir(). false si fallo.
bool bajar_https(const std::wstring& url, const std::function<bool(const char*, DWORD)>& recibir) {
    wchar_t host[256] = L"", ruta[2048] = L"";
    URL_COMPONENTSW uc{sizeof uc};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = ruta;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    HINTERNET s = WinHttpOpen(L"kciwapp-setup", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 60000, 60000);
    bool ok = false;
    HINTERNET con = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", ruta, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD estado = 0, largo = sizeof estado;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &estado, &largo, WINHTTP_NO_HEADER_INDEX);
        if (estado == 200) {
            ok = true;
            std::string buf;
            for (;;) {
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
                if (!recibir(buf.data(), leido)) {
                    ok = false;
                    break;
                }
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(s);
    return ok;
}

// "md5 tamano url ruta" por linea.
std::vector<Archivo> parsear_manifiesto(const std::string& s) {
    std::vector<Archivo> lista;
    std::istringstream in(s);
    std::string linea;
    while (std::getline(in, linea)) {
        if (linea.empty() || linea[0] == '#') continue;
        std::istringstream l(linea);
        Archivo a;
        std::string ruta, url;
        if (!(l >> a.md5 >> a.tamano >> url)) continue;
        a.url = ancho(url);
        std::getline(l, ruta);
        while (!ruta.empty() && ruta.front() == ' ') ruta.erase(ruta.begin());
        while (!ruta.empty() && (ruta.back() == '\r' || ruta.back() == ' ')) ruta.pop_back();
        if (ruta.empty() || a.md5.size() != 32 || url.rfind("https://", 0) != 0) continue;
        for (auto& c : ruta)
            if (c == '/') c = '\\';
        a.ruta = ancho(ruta);
        lista.push_back(a);
    }
    return lista;
}

void crear_carpetas(const std::wstring& ruta_archivo) {
    for (size_t i = 3; i < ruta_archivo.size(); i++)
        if (ruta_archivo[i] == L'\\') CreateDirectoryW(ruta_archivo.substr(0, i).c_str(), nullptr);
}

bool escribir_archivo(const std::wstring& ruta, const std::string& datos) {
    crear_carpetas(ruta);
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD e = 0;
    WriteFile(h, datos.data(), (DWORD)datos.size(), &e, nullptr);
    CloseHandle(h);
    return e == datos.size();
}

// icacls <carpeta> /grant *S-1-5-32-545:(OI)(CI)M  (Users: modificar, heredado)
void permitir_escritura(const std::wstring& carpeta) {
    std::wstring linea = L"icacls.exe \"" + carpeta + L"\" /grant *S-1-5-32-545:(OI)(CI)M /T /Q";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, linea.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void acceso_directo(const std::wstring& lnk, const std::wstring& exe, const std::wstring& carpeta) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return;
    sl->SetPath(exe.c_str());
    sl->SetWorkingDirectory(carpeta.c_str());
    sl->SetDescription(L"kciwapp");
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        pf->Save(lnk.c_str(), TRUE);
        pf->Release();
    }
    sl->Release();
}

std::wstring carpeta_especial(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

// ---- la bajada, en su hilo -------------------------------------------------

void avisar(int que) { PostMessageW(g_hwnd, WM_AVANCE, que, 0); }

void fallar(const std::wstring& e) {
    EnterCriticalSection(&g_cs);
    g_error = e;
    LeaveCriticalSection(&g_cs);
    avisar(2);
}

bool es_whisper(const std::wstring& ruta) { return ruta.rfind(L"whisper\\", 0) == 0; }
bool es_traductor(const std::wstring& ruta) { return ruta.rfind(L"translate\\", 0) == 0; }

void hilo_bajada(std::wstring carpeta, bool escritorio, bool whisper, bool traductor) {
    std::string manifiesto;
    if (!bajar_https(URL_MANIFIESTO, [&](const char* d, DWORD n) {
            manifiesto.append(d, n);
            return true;
        })) {
        fallar(L"Could not reach github.com");
        return;
    }
    auto lista = parsear_manifiesto(manifiesto);
    if (lista.empty()) {
        fallar(L"Bad manifest from the server");
        return;
    }
    if (!whisper || !traductor) {
        // Sin transcripcion / traduccion: esas carpetas no se bajan (el
        // cliente tampoco las pide despues al actualizar, si no existen).
        std::vector<Archivo> sin;
        for (auto& a : lista)
            if (!(!whisper && es_whisper(a.ruta)) && !(!traductor && es_traductor(a.ruta))) sin.push_back(a);
        lista = sin;
    }
    long long total = 0;
    for (auto& a : lista) total += a.tamano;
    EnterCriticalSection(&g_cs);
    g_total = total;
    g_cuantos = (int)lista.size();
    LeaveCriticalSection(&g_cs);
    long long acumulado = 0;
    for (const Archivo& a : lista) {
        if (g_cancelar) return;
        std::wstring destino = carpeta + L"\\" + a.ruta;
        crear_carpetas(destino);
        EnterCriticalSection(&g_cs);
        g_actual = a.ruta;
        LeaveCriticalSection(&g_cs);
        HANDLE f = CreateFileW(destino.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            fallar(L"Cannot write " + destino);
            return;
        }
        Md5 md5;
        long long bytes = 0;
        ULONGLONG ultimo = 0;
        bool ok = bajar_https(a.url, [&](const char* d, DWORD n) {
            if (g_cancelar) return false;
            DWORD e = 0;
            if (!WriteFile(f, d, n, &e, nullptr) || e != n) return false;
            md5.meter(d, n);
            bytes += n;
            EnterCriticalSection(&g_cs);
            g_bajado = acumulado + bytes;
            LeaveCriticalSection(&g_cs);
            ULONGLONG t = GetTickCount64();
            if (t - ultimo > 100) {
                ultimo = t;
                avisar(0);
            }
            return true;
        });
        CloseHandle(f);
        if (g_cancelar) {
            DeleteFileW(destino.c_str());
            return;
        }
        if (!ok || md5.terminar() != a.md5) {
            DeleteFileW(destino.c_str());
            fallar(L"Download failed: " + a.ruta);
            return;
        }
        acumulado += a.tamano;
        EnterCriticalSection(&g_cs);
        g_bajado = acumulado;
        g_hechos++;
        LeaveCriticalSection(&g_cs);
        avisar(0);
    }
    // El manifiesto instalado, para el check for updates del cliente.
    escribir_archivo(carpeta + L"\\" + MANIFIESTO, manifiesto);
    // Users puede escribir en la carpeta (datos\, cuentas.json, actualizaciones).
    permitir_escritura(carpeta);
    // Accesos directos: menu inicio siempre, escritorio si se pidio.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring exe = carpeta + L"\\kciwapp2.exe";
    std::wstring inicio = carpeta_especial(FOLDERID_Programs);
    if (!inicio.empty()) acceso_directo(inicio + L"\\kciwapp.lnk", exe, carpeta);
    if (escritorio) {
        std::wstring esc = carpeta_especial(FOLDERID_Desktop);
        if (!esc.empty()) acceso_directo(esc + L"\\kciwapp.lnk", exe, carpeta);
    }
    CoUninitialize();
    avisar(1);
}

// ---- la ventana --------------------------------------------------------------

// Geometria (en px logicos, se escala al dibujar).
const int ANCHO = 560, ALTO = 410, MARGEN = 32;
struct Geo {
    RECT campo, examinar, casilla, casilla2, casilla3, boton, boton2, barra;
};

Geo geo() {
    Geo q;
    int x = E(MARGEN), w = E(ANCHO - 2 * MARGEN);
    q.campo = {x, E(140), x + w - E(110), E(140 + 36)};
    q.examinar = {x + w - E(96), E(140), x + w, E(140 + 36)};
    q.casilla = {x, E(196), x + E(20), E(216)};
    q.casilla2 = {x, E(228), x + E(20), E(248)};
    q.casilla3 = {x, E(258), x + E(20), E(278)};
    q.boton = {x + w - E(140), E(ALTO - MARGEN - 40), x + w, E(ALTO - MARGEN)};
    q.boton2 = {x + w - E(140) - E(12) - E(120), E(ALTO - MARGEN - 40), x + w - E(140) - E(12), E(ALTO - MARGEN)};
    q.barra = {x, E(200), x + w, E(210)};
    return q;
}

HFONT fuente(int tam, int peso = FW_NORMAL) {
    return CreateFontW(-E((float)tam), 0, 0, 0, peso, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void texto(HDC dc, const std::wstring& t, int x, int y, int tam, COLORREF c, int peso = FW_NORMAL, int ancho_max = 0, bool centrado = false, RECT* en = nullptr) {
    HFONT f = fuente(tam, peso);
    HGDIOBJ viejo = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    if (en) {
        DrawTextW(dc, t.c_str(), -1, en, DT_SINGLELINE | DT_VCENTER | (centrado ? DT_CENTER : DT_LEFT) | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        RECT r = {x, y, x + (ancho_max > 0 ? ancho_max : E(ANCHO)), y + E(100)};
        DrawTextW(dc, t.c_str(), -1, &r, DT_SINGLELINE | DT_NOPREFIX | (ancho_max > 0 ? DT_END_ELLIPSIS | DT_PATH_ELLIPSIS : 0));
    }
    SelectObject(dc, viejo);
    DeleteObject(f);
}

void relleno(HDC dc, const RECT& r, COLORREF c, int radio) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ vb = SelectObject(dc, b), vp = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radio, radio);
    SelectObject(dc, vb);
    SelectObject(dc, vp);
    DeleteObject(b);
    DeleteObject(p);
}

void borde(HDC dc, const RECT& r, COLORREF c, int radio) {
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ vp = SelectObject(dc, p), vb = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radio, radio);
    SelectObject(dc, vp);
    SelectObject(dc, vb);
    DeleteObject(p);
}

void boton(HDC dc, const RECT& r, const wchar_t* t, bool lleno) {
    if (lleno) relleno(dc, r, ACCENT, E(8));
    else borde(dc, r, RGB(0x3b, 0x4a, 0x54), E(8));
    RECT rr = r;
    texto(dc, t, 0, 0, 14, lleno ? RGB(255, 255, 255) : TXT, lleno ? FW_SEMIBOLD : FW_NORMAL, 0, true, &rr);
}

void dibujar(HDC dc, const RECT& area) {
    HBRUSH fondo = CreateSolidBrush(BG);
    FillRect(dc, &area, fondo);
    DeleteObject(fondo);
    RECT franja = {0, 0, area.right, E(6)};
    HBRUSH acc = CreateSolidBrush(ACCENT);
    FillRect(dc, &franja, acc);
    DeleteObject(acc);
    Geo q = geo();
    int x = E(MARGEN);
    texto(dc, L"Install kciwapp", x, E(40), 26, TXT, FW_LIGHT);
    {
        int mb = 400 + (g_whisper ? 2700 : 0) + (g_traductor ? 5300 : 0);
        wchar_t t[160];
        if (mb >= 1000) swprintf(t, 160, L"Everything downloads from GitHub%s (about %.1f GB).", (g_whisper || g_traductor) ? L" and Hugging Face" : L"", mb / 1000.0);
        else swprintf(t, 160, L"Everything downloads from GitHub (about %d MB).", mb);
        texto(dc, t, x, E(84), 12, DIM);
    }

    EnterCriticalSection(&g_cs);
    std::wstring actual = g_actual, error = g_error;
    long long total = g_total, bajado = g_bajado;
    int hechos = g_hechos, cuantos = g_cuantos;
    LeaveCriticalSection(&g_cs);

    if (g_paso == ELEGIR) {
        texto(dc, L"Install to", x, E(116), 13, DIM);
        relleno(dc, q.campo, CAMPO, E(8));
        RECT rc = q.campo;
        rc.left += E(12);
        rc.right -= E(12);
        texto(dc, g_carpeta, 0, 0, 14, TXT, FW_NORMAL, 0, false, &rc);
        boton(dc, q.examinar, L"Browse...", false);
        // Las casillas: acceso directo y whisper.
        auto casilla = [&](const RECT& r, bool marcada, const wchar_t* t, const wchar_t* nota) {
            borde(dc, r, marcada ? ACCENT : DIM, E(5));
            if (marcada) {
                RECT in = {r.left + E(4), r.top + E(4), r.right - E(4), r.bottom - E(4)};
                relleno(dc, in, ACCENT, E(3));
            }
            texto(dc, t, r.right + E(10), r.top, 13, TXT);
            if (nota) {
                HFONT f = fuente(13);
                HGDIOBJ v = SelectObject(dc, f);
                SIZE tam;
                GetTextExtentPoint32W(dc, t, (int)wcslen(t), &tam);
                SelectObject(dc, v);
                DeleteObject(f);
                texto(dc, nota, r.right + E(10) + tam.cx + E(8), r.top + E(1), 12, DIM);
            }
        };
        casilla(q.casilla, g_acceso_escritorio, L"Desktop shortcut", nullptr);
        casilla(q.casilla2, g_whisper, L"Voice note transcription", L"whisper, 2.7 GB, needs an NVIDIA GPU");
        casilla(q.casilla3, g_traductor, L"Message translation", L"llama.cpp + Qwen 7B, 5.3 GB, needs an NVIDIA GPU");
        boton(dc, q.boton, L"Install", true);
    } else if (g_paso == BAJANDO) {
        texto(dc, actual.empty() ? L"Getting the file list..." : actual, x, E(150), 13, DIM, FW_NORMAL, E(ANCHO - 2 * MARGEN));
        relleno(dc, q.barra, CAMPO, E(5));
        if (total > 0 && bajado > 0) {
            RECT lleno = q.barra;
            lleno.right = lleno.left + (LONG)((lleno.right - lleno.left) * (double)bajado / (double)total);
            relleno(dc, lleno, ACCENT, E(5));
        }
        std::wstring t = megas(bajado) + L" / " + megas(total) + L"   ·   " + std::to_wstring(hechos) + L" / " + std::to_wstring(cuantos) + L" files";
        texto(dc, t, x, E(222), 12, DIM);
        boton(dc, q.boton, L"Cancel", false);
    } else if (g_paso == LISTO) {
        texto(dc, L"Done. kciwapp is installed in", x, E(140), 14, TXT);
        texto(dc, g_carpeta, x, E(164), 13, DIM, FW_NORMAL, E(ANCHO - 2 * MARGEN));
        boton(dc, q.boton2, L"Close", false);
        boton(dc, q.boton, L"Open kciwapp", true);
    } else {
        texto(dc, L"Something went wrong:", x, E(140), 14, TXT);
        texto(dc, error, x, E(164), 13, ROJO, FW_NORMAL, E(ANCHO - 2 * MARGEN));
        boton(dc, q.boton2, L"Close", false);
        boton(dc, q.boton, L"Retry", true);
    }
}

bool dentro(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

bool clickeable(int x, int y) {
    Geo q = geo();
    if (g_paso == ELEGIR) return dentro(q.campo, x, y) || dentro(q.examinar, x, y) || dentro(q.boton, x, y) ||
                              (y >= q.casilla.top && y < q.casilla.bottom && x >= q.casilla.left && x < q.casilla.left + E(150)) ||
                              (y >= q.casilla2.top && y < q.casilla2.bottom && x >= q.casilla2.left && x < q.casilla2.left + E(200)) ||
                              (y >= q.casilla3.top && y < q.casilla3.bottom && x >= q.casilla3.left && x < q.casilla3.left + E(200));
    if (g_paso == BAJANDO) return dentro(q.boton, x, y);
    return dentro(q.boton, x, y) || dentro(q.boton2, x, y);
}

void elegir_carpeta() {
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileDialog, (void**)&d))) return;
    DWORD op = 0;
    d->GetOptions(&op);
    d->SetOptions(op | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Install kciwapp in...");
    if (SUCCEEDED(d->Show(g_hwnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                g_carpeta = p;
                // Si eligio una carpeta que no es "nuestra", adentro va KciWAPP.
                size_t corte = g_carpeta.find_last_of(L'\\');
                std::wstring nombre = corte == std::wstring::npos ? g_carpeta : g_carpeta.substr(corte + 1);
                if (_wcsicmp(nombre.c_str(), L"kciwapp") != 0) g_carpeta += L"\\KciWAPP";
                CoTaskMemFree(p);
            }
            it->Release();
        }
    }
    d->Release();
}

void empezar() {
    g_paso = BAJANDO;
    g_cancelar = false;
    EnterCriticalSection(&g_cs);
    g_actual.clear();
    g_error.clear();
    g_total = g_bajado = 0;
    g_hechos = g_cuantos = 0;
    LeaveCriticalSection(&g_cs);
    std::thread(hilo_bajada, g_carpeta, g_acceso_escritorio, g_whisper, g_traductor).detach();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void click(int x, int y) {
    Geo q = geo();
    if (g_paso == ELEGIR) {
        if (dentro(q.campo, x, y) || dentro(q.examinar, x, y)) elegir_carpeta();
        else if (y >= q.casilla.top && y < q.casilla.bottom && x >= q.casilla.left && x < q.casilla.left + E(150)) g_acceso_escritorio = !g_acceso_escritorio;
        else if (y >= q.casilla2.top && y < q.casilla2.bottom && x >= q.casilla2.left && x < q.casilla2.left + E(200)) g_whisper = !g_whisper;
        else if (y >= q.casilla3.top && y < q.casilla3.bottom && x >= q.casilla3.left && x < q.casilla3.left + E(200)) g_traductor = !g_traductor;
        else if (dentro(q.boton, x, y)) empezar();
    } else if (g_paso == BAJANDO) {
        if (dentro(q.boton, x, y)) {
            g_cancelar = true;
            g_paso = ELEGIR;
        }
    } else if (g_paso == LISTO) {
        if (dentro(q.boton, x, y)) {
            // Via explorer, asi abre sin la elevacion del instalador.
            ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + g_carpeta + L"\\kciwapp2.exe\"").c_str(), nullptr, SW_SHOWNORMAL);
            PostQuitMessage(0);
        } else if (dentro(q.boton2, x, y)) PostQuitMessage(0);
    } else {
        if (dentro(q.boton, x, y)) empezar();
        else if (dentro(q.boton2, x, y)) PostQuitMessage(0);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK ventana(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT r;
            GetClientRect(h, &r);
            // Doble buffer para que no parpadee.
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
            HGDIOBJ viejo = SelectObject(mem, bmp);
            dibujar(mem, r);
            BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, viejo);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONDOWN: click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
        case WM_MOUSEMOVE: g_mano = clickeable(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, g_mano ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_AVANCE:
            if (wp == 1) g_paso = LISTO;
            else if (wp == 2) g_paso = ERROR_;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                Geo q = geo();
                click(q.boton.left + 1, q.boton.top + 1);
            } else if (wp == VK_ESCAPE && g_paso != BAJANDO) PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED: {
            g_escala = HIWORD(wp) / 96.0f;
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            if (g_paso == BAJANDO) g_cancelar = true;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitializeCriticalSection(&g_cs);
    // Va a Program Files (el instalador pide elevacion); despues se le da
    // permiso de escritura a Users sobre la carpeta, porque el cliente guarda
    // ahi sus datos y corre sin admin.
    g_carpeta = carpeta_especial(FOLDERID_ProgramFiles) + L"\\KciWAPP";
    // Titulo y menus oscuros.
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll"))
        if (auto f = (int(WINAPI*)(int))GetProcAddress(ux, MAKEINTRESOURCEA(135))) f(2);
    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = ventana;
    wc.hInstance = inst;
    wc.lpszClassName = L"kciwapp-setup";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"kciwapp", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, inst, nullptr);
    g_hwnd = h;
    g_escala = GetDpiForWindow(h) / 96.0f;
    RECT r = {0, 0, E(ANCHO), E(ALTO)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, GetDpiForWindow(h));
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(h, nullptr, (sw - (r.right - r.left)) / 2, (sh - (r.bottom - r.top)) / 2, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
    // Barra de titulo oscura (DWMWA_USE_IMMERSIVE_DARK_MODE = 20).
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
        if (auto f = (HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD))GetProcAddress(dwm, "DwmSetWindowAttribute")) {
            BOOL oscuro = TRUE;
            f(h, 20, &oscuro, sizeof oscuro);
        }
    ShowWindow(h, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
