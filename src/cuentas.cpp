#include "cuentas.h"

#include <windows.h>

#include "json.h"
#include "red.h"

namespace {

std::wstring g_exe;
std::vector<Cuenta> g_cuentas;
int g_activa = -1;

std::wstring ruta_cuentas() { return g_exe + L"\\cuentas.json"; }

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
    DWORD e = 0;
    WriteFile(h, datos.data(), (DWORD)datos.size(), &e, nullptr);
    CloseHandle(h);
    return e == datos.size();
}

void guardar() {
    std::string s = "{\"cuentas\": [\n";
    for (size_t i = 0; i < g_cuentas.size(); i++) {
        const Cuenta& c = g_cuentas[i];
        s += "  {\"nombre\": " + json_texto(angosto(c.nombre)) + ", \"host\": " + json_texto(c.host) +
             ", \"puerto\": " + std::to_string(c.puerto) + ", \"token\": " + json_texto(c.token) +
             ", \"carpeta\": " + json_texto(angosto(c.carpeta)) + "}" + (i + 1 < g_cuentas.size() ? ",\n" : "\n");
    }
    s += "]}\n";
    escribir(ruta_cuentas(), s);
}

// El servidor.json de antes (una sola cuenta) pasa a ser la cuenta 1, con
// la carpeta datos\ que ya tenia.
void migrar_servidor_json() {
    std::wstring vieja = g_exe + L"\\servidor.json";
    Json j = Json::parsear(leer(vieja));
    if (j.nulo()) return;
    Cuenta c;
    c.host = j["host"].str("127.0.0.1");
    if (c.host == "localhost") c.host = "127.0.0.1";
    c.puerto = (int)j["puerto"].entero(c.local() ? 8477 : 8080);
    c.token = j["token"].str();
    c.carpeta = L"datos";
    if (!c.local() && c.token.empty()) return;
    g_cuentas.push_back(c);
    guardar();
    DeleteFileW(vieja.c_str());
    red::registrar("cuentas: servidor.json migrado a cuentas.json");
}

}  // namespace

namespace cuentas {

void cargar(const std::wstring& carpeta_exe) {
    g_exe = carpeta_exe;
    g_cuentas.clear();
    g_activa = -1;
    Json j = Json::parsear(leer(ruta_cuentas()));
    const Json& l = j["cuentas"];
    for (size_t i = 0; i < l.largo(); i++) {
        const Json& e = l[i];
        Cuenta c;
        c.nombre = ancho(e["nombre"].str());
        c.host = e["host"].str("127.0.0.1");
        c.puerto = (int)e["puerto"].entero(c.local() ? 8477 : 8080);
        c.token = e["token"].str();
        c.carpeta = ancho(e["carpeta"].str("datos"));
        g_cuentas.push_back(c);
    }
    if (g_cuentas.empty()) migrar_servidor_json();
}

const std::vector<Cuenta>& lista() { return g_cuentas; }

int agregar(Cuenta c) {
    // Carpeta: la primera usa datos\ a secas; las demas datos\cuenta-N.
    bool usada_base = false;
    int n_max = 1;
    for (const Cuenta& o : g_cuentas) {
        if (_wcsicmp(o.carpeta.c_str(), L"datos") == 0) usada_base = true;
        if (o.carpeta.rfind(L"datos\\cuenta-", 0) == 0) n_max = std::max(n_max, _wtoi(o.carpeta.c_str() + 13));
    }
    c.carpeta = usada_base ? L"datos\\cuenta-" + std::to_wstring(n_max + 1) : L"datos";
    if (c.local()) {
        // Un puerto por cuenta local, por si dos quedan abiertas a la vez.
        int p = 8477;
        for (bool libre = false; !libre; p++) {
            libre = true;
            for (const Cuenta& o : g_cuentas)
                if (o.local() && o.puerto == p) libre = false;
            if (libre) break;
        }
        c.puerto = p;
    }
    g_cuentas.push_back(c);
    guardar();
    return (int)g_cuentas.size() - 1;
}

void elegir(int i) { g_activa = (i >= 0 && i < (int)g_cuentas.size()) ? i : -1; }

int activa() { return g_activa; }

const Cuenta* actual() { return g_activa < 0 ? nullptr : &g_cuentas[g_activa]; }

std::wstring carpeta_activa() {
    std::wstring r = g_exe + L"\\" + (g_activa < 0 ? L"datos" : g_cuentas[g_activa].carpeta);
    CreateDirectoryW((g_exe + L"\\datos").c_str(), nullptr);
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
}

void poner_nombre(int i, const std::wstring& nombre) {
    if (i < 0 || i >= (int)g_cuentas.size() || g_cuentas[i].nombre == nombre) return;
    g_cuentas[i].nombre = nombre;
    guardar();
}

std::wstring etiqueta(const Cuenta& c) {
    if (!c.nombre.empty()) return c.nombre;
    return c.local() ? L"This computer" : ancho(c.host) + L":" + std::to_wstring(c.puerto);
}

std::wstring detalle(const Cuenta& c) {
    if (c.nombre.empty()) return L"Not linked yet";
    return c.local() ? L"This computer" : ancho(c.host) + L":" + std::to_wstring(c.puerto);
}

}  // namespace cuentas
