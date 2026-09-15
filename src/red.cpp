#include "red.h"

#include <winhttp.h>

#include <cstdio>
#include <mutex>
#include <thread>

namespace {

std::wstring g_host;
int g_puerto = 8080;
std::string g_token;
HINTERNET g_sesion = nullptr;
HWND g_ventana = nullptr;
std::mutex g_mu;

HINTERNET sesion() {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_sesion) {
        g_sesion = WinHttpOpen(L"kciwapp2", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    }
    return g_sesion;
}

}  // namespace

std::wstring ancho(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}

std::string angosto(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

namespace red {

void configurar(const std::wstring& host, int puerto, const std::string& token) {
    g_host = host;
    g_puerto = puerto;
    g_token = token;
}

bool configurado() { return !g_host.empty() && !g_token.empty(); }

Respuesta pedir(const wchar_t* metodo, const std::wstring& ruta, const std::string& cuerpo, const wchar_t* tipo,
                int espera_ms) {
    Respuesta r;
    HINTERNET s = sesion();
    if (!s) return r;
    WinHttpSetTimeouts(s, 5000, 5000, espera_ms, espera_ms);
    HINTERNET con = WinHttpConnect(s, g_host.c_str(), (INTERNET_PORT)g_puerto, 0);
    if (!con) return r;
    HINTERNET req = WinHttpOpenRequest(con, metodo, ruta.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (req) {
        std::wstring cabeceras = L"X-Token: " + ancho(g_token) + L"\r\n";
        if (!cuerpo.empty()) cabeceras += std::wstring(L"Content-Type: ") + tipo + L"\r\n";
        BOOL ok = WinHttpSendRequest(req, cabeceras.c_str(), (DWORD)-1, (LPVOID)cuerpo.data(), (DWORD)cuerpo.size(),
                                     (DWORD)cuerpo.size(), 0) &&
                  WinHttpReceiveResponse(req, nullptr);
        if (ok) {
            DWORD estado = 0, largo = sizeof estado;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &estado, &largo, WINHTTP_NO_HEADER_INDEX);
            r.estado = (int)estado;
            wchar_t ct[256];
            DWORD lct = sizeof ct;
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ct, &lct,
                                    WINHTTP_NO_HEADER_INDEX))
                r.tipo = angosto(ct);
            for (;;) {
                DWORD disponible = 0;
                if (!WinHttpQueryDataAvailable(req, &disponible) || disponible == 0) break;
                size_t antes = r.cuerpo.size();
                r.cuerpo.resize(antes + disponible);
                DWORD leido = 0;
                if (!WinHttpReadData(req, r.cuerpo.data() + antes, disponible, &leido)) break;
                r.cuerpo.resize(antes + leido);
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(con);
    return r;
}

Respuesta obtener(const std::wstring& ruta, int espera_ms) { return pedir(L"GET", ruta, "", L"", espera_ms); }

Respuesta mandar_json(const std::wstring& ruta, const std::string& json) {
    return pedir(L"POST", ruta, json, L"application/json");
}

Respuesta mandar_archivo(const std::wstring& ruta, const std::string& chat, const std::string& nombre,
                         const std::string& mime, const std::string& datos, const std::string& texto,
                         const std::string& cita, const std::string& tipo, int segundos) {
    const std::string borde = "----kciwapp2-" + std::to_string(GetTickCount64());
    std::string c;
    auto campo = [&](const char* nombre_campo, const std::string& valor) {
        if (valor.empty()) return;
        c += "--" + borde + "\r\nContent-Disposition: form-data; name=\"" + nombre_campo + "\"\r\n\r\n" + valor + "\r\n";
    };
    campo("chat", chat);
    campo("texto", texto);
    campo("cita_id", cita);
    campo("tipo", tipo);
    campo("mime", mime);
    if (segundos > 0) campo("segundos", std::to_string(segundos));
    c += "--" + borde + "\r\nContent-Disposition: form-data; name=\"archivo\"; filename=\"" + nombre +
         "\"\r\nContent-Type: " + (mime.empty() ? "application/octet-stream" : mime) + "\r\n\r\n";
    c += datos;
    c += "\r\n--" + borde + "--\r\n";
    std::wstring ct = L"multipart/form-data; boundary=" + ancho(borde);
    return pedir(L"POST", ruta, c, ct.c_str(), 600000);
}

void en_fondo(std::function<void()> f) {
    std::thread([f = std::move(f)] {
        try {
            f();
        } catch (const std::exception& e) {
            registrar(std::string("excepcion en hilo de fondo: ") + e.what());
        }
    }).detach();
}

void en_ui(std::function<void()> f) {
    auto* p = new std::function<void()>(std::move(f));
    if (!g_ventana || !PostMessageW(g_ventana, WM_TRABAJO, 0, (LPARAM)p)) delete p;
}

void anotar_ventana(HWND h) { g_ventana = h; }

void atender_trabajo(LPARAM lp) {
    auto* p = (std::function<void()>*)lp;
    try {
        (*p)();
    } catch (const std::exception& e) {
        registrar(std::string("excepcion en trabajo de UI: ") + e.what());
    }
    delete p;
}

void registrar(const std::string& linea) {
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    // Al lado del exe: portable\datos\debug.log.
    static std::wstring ruta;
    if (ruta.empty()) {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        ruta = buf;
        ruta = ruta.substr(0, ruta.find_last_of(L"\\/")) + L"\\datos";
        CreateDirectoryW(ruta.c_str(), nullptr);
        ruta += L"\\debug.log";
    }
    FILE* f = _wfopen(ruta.c_str(), L"a");
    if (f) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, linea.c_str());
        fclose(f);
    }
}

}  // namespace red
