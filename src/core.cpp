#include "core.h"

#include <bcrypt.h>

#include <cstdio>

#include "json.h"
#include "red.h"

#pragma comment(lib, "bcrypt.lib")

namespace {

HANDLE g_job = nullptr;
HANDLE g_proceso = nullptr;
const int PUERTO = 8477;

std::string leer(const std::wstring& ruta) {
    std::string s;
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    char buf[4096];
    DWORD leido = 0;
    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) s.append(buf, leido);
    CloseHandle(h);
    return s;
}

bool escribir(const std::wstring& ruta, const std::string& datos) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD escrito = 0;
    WriteFile(h, datos.data(), (DWORD)datos.size(), &escrito, nullptr);
    CloseHandle(h);
    return escrito == datos.size();
}

std::string token_nuevo() {
    unsigned char b[24];
    BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    const char* abc = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string t;
    for (unsigned char c : b) t += abc[c % 62];
    return t;
}

// Rutas con \ escapadas para JSON.
std::string json_ruta(const std::wstring& r) {
    std::string s = angosto(r), o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else o += c;
    }
    return o;
}

}  // namespace

namespace core {

bool iniciar(const std::wstring& carpeta_exe, std::wstring& host, int& puerto, std::string& token) {
    std::wstring exe = carpeta_exe + L"\\core\\kciwapp-core.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    std::wstring datos = carpeta_exe + L"\\datos";
    CreateDirectoryW(datos.c_str(), nullptr);
    // El token: se inventa una vez y queda en datos\core.json.
    std::wstring ruta_cfg = datos + L"\\core.json";
    Json j = Json::parsear(leer(ruta_cfg));
    token = j["token"].str();
    if (token.size() < 20) token = token_nuevo();
    std::string cfg = "{\n"
                      "  \"escucha\": \"127.0.0.1:" + std::to_string(PUERTO) + "\",\n"
                      "  \"token\": \"" + token + "\",\n"
                      "  \"sqlite\": \"" + json_ruta(datos + L"\\core.sqlite3") + "\",\n"
                      "  \"store\": \"" + json_ruta(datos + L"\\store.db") + "\",\n"
                      "  \"media\": \"" + json_ruta(datos + L"\\media-core") + "\",\n"
                      "  \"historia\": \"reciente\",\n"
                      "  \"bajar_historia\": false\n"
                      "}\n";
    escribir(ruta_cfg, cfg);
    // Job object: si el cliente muere, el core tambien.
    if (!g_job) {
        g_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    std::wstring linea = L"\"" + exe + L"\" \"" + ruta_cfg + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    // El log del core va a datos\core.log.
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE log = CreateFileW((datos + L"\\core.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError = log;
    }
    BOOL ok = CreateProcessW(nullptr, linea.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, (carpeta_exe + L"\\core").c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        red::registrar("core: no se pudo lanzar kciwapp-core.exe");
        return false;
    }
    AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_proceso = pi.hProcess;
    host = L"127.0.0.1";
    puerto = PUERTO;
    red::registrar("core lanzado (pid " + std::to_string(pi.dwProcessId) + ")");
    return true;
}

void cerrar() {
    if (g_proceso) {
        TerminateProcess(g_proceso, 0);
        CloseHandle(g_proceso);
        g_proceso = nullptr;
    }
    if (g_job) {
        CloseHandle(g_job);
        g_job = nullptr;
    }
}

bool activo() { return g_proceso != nullptr; }

}  // namespace core
