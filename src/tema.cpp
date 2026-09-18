#include "tema.h"

#include <windows.h>

#include <atomic>

#include "json.h"
#include "red.h"

namespace {

Ajustes g_ajustes;
std::wstring g_carpeta;
std::atomic<unsigned> g_revision{1};

Paleta preset(const std::string& tema) {
    Paleta p;  // dark por defecto
    if (tema == "black") {
        p.bg_app = 0x000000; p.bg_panel = 0x0d0d0d; p.bg_chat = 0x000000; p.bg_hover = 0x161616;
        p.bg_sel = 0x1f1f1f; p.bg_campo = 0x1a1a1a; p.burbuja_mia = 0x0b4d3f; p.burbuja_otra = 0x1a1a1a;
        p.divisor = 0x111111; p.borde = 0x1a1a1a;
    } else if (tema == "light") {
        p.bg_app = 0xffffff; p.bg_panel = 0xf0f2f5; p.bg_chat = 0xefeae2; p.bg_hover = 0xf5f6f6;
        p.bg_sel = 0xf0f2f5; p.bg_campo = 0xffffff; p.burbuja_mia = 0xd9fdd3; p.burbuja_otra = 0xffffff;
        p.txt = 0x111b21; p.txt_dim = 0x667781; p.acento = 0x00a884; p.tick_azul = 0x53bdeb;
        p.divisor = 0xffffff; p.borde = 0xe9edef; p.claro = true;
    } else if (tema == "blue") {
        p.bg_app = 0x0f1a2b; p.bg_panel = 0x1a2a40; p.bg_chat = 0x0a1220; p.bg_hover = 0x1a2a40;
        p.bg_sel = 0x24384f; p.bg_campo = 0x24384f; p.burbuja_mia = 0x1f4e79; p.burbuja_otra = 0x1a2a40;
        p.acento = 0x4da3ff; p.divisor = 0x152238; p.borde = 0x1f2f45;
    } else if (tema == "orange") {
        p.bg_app = 0x1b1410; p.bg_panel = 0x2a201a; p.bg_chat = 0x120d0a; p.bg_hover = 0x2a201a;
        p.bg_sel = 0x3a2c22; p.bg_campo = 0x3a2c22; p.burbuja_mia = 0x7a3e10; p.burbuja_otra = 0x2a201a;
        p.acento = 0xff8c32; p.tick_azul = 0xffb066; p.divisor = 0x201811; p.borde = 0x2f241c;
    } else if (tema == "green") {
        p.bg_app = 0x0f1a14; p.bg_panel = 0x1a2b21; p.bg_chat = 0x0a120e; p.bg_hover = 0x1a2b21;
        p.bg_sel = 0x24392d; p.bg_campo = 0x24392d; p.burbuja_mia = 0x1f6b3a; p.burbuja_otra = 0x1a2b21;
        p.acento = 0x3ddc84; p.divisor = 0x152219; p.borde = 0x1f3026;
    } else if (tema == "red") {
        p.bg_app = 0x1c0f10; p.bg_panel = 0x2c1a1c; p.bg_chat = 0x130a0b; p.bg_hover = 0x2c1a1c;
        p.bg_sel = 0x3c2426; p.bg_campo = 0x3c2426; p.burbuja_mia = 0x7a1f26; p.burbuja_otra = 0x2c1a1c;
        p.acento = 0xff5c6a; p.tick_azul = 0xff9aa3; p.divisor = 0x221214; p.borde = 0x33201f;
    } else if (tema == "custom") {
        p = g_ajustes.custom;
    }
    return p;
}

Json paleta_a_json(const Paleta& p) {
    Json j;
    j.tipo = Json::Objeto;
    auto pon = [&](const char* k, unsigned c) {
        Json v;
        v.tipo = Json::Texto;
        v.s = ajustes::hex_de(c);
        j.objeto[k] = v;
    };
    pon("bg_app", p.bg_app); pon("bg_panel", p.bg_panel); pon("bg_chat", p.bg_chat); pon("bg_hover", p.bg_hover);
    pon("bg_sel", p.bg_sel); pon("bg_campo", p.bg_campo); pon("burbuja_mia", p.burbuja_mia);
    pon("burbuja_otra", p.burbuja_otra); pon("txt", p.txt); pon("txt_dim", p.txt_dim); pon("acento", p.acento);
    pon("tick_azul", p.tick_azul); pon("divisor", p.divisor); pon("borde", p.borde);
    Json c;
    c.tipo = Json::Booleano;
    c.b = p.claro;
    j.objeto["claro"] = c;
    return j;
}

Paleta paleta_de_json(const Json& j, Paleta base) {
    auto lee = [&](const char* k, unsigned& c) { c = ajustes::color_de_hex(j[k].str(), c); };
    lee("bg_app", base.bg_app); lee("bg_panel", base.bg_panel); lee("bg_chat", base.bg_chat);
    lee("bg_hover", base.bg_hover); lee("bg_sel", base.bg_sel); lee("bg_campo", base.bg_campo);
    lee("burbuja_mia", base.burbuja_mia); lee("burbuja_otra", base.burbuja_otra); lee("txt", base.txt);
    lee("txt_dim", base.txt_dim); lee("acento", base.acento); lee("tick_azul", base.tick_azul);
    lee("divisor", base.divisor); lee("borde", base.borde);
    base.claro = j["claro"].bul(base.claro);
    return base;
}


}  // namespace

std::string serializar(const Json& j) {
    switch (j.tipo) {
        case Json::Nulo: return "null";
        case Json::Booleano: return j.b ? "true" : "false";
        case Json::Numero: {
            char buf[32];
            snprintf(buf, sizeof buf, "%g", j.n);
            return buf;
        }
        case Json::Texto: return json_texto(j.s);
        case Json::Lista: {
            std::string s = "[";
            for (size_t i = 0; i < j.lista.size(); i++) s += (i ? "," : "") + serializar(j.lista[i]);
            return s + "]";
        }
        case Json::Objeto: {
            std::string s = "{";
            bool primero = true;
            for (auto& [k, v] : j.objeto) {
                s += (primero ? "" : ",") + json_texto(k) + ":" + serializar(v);
                primero = false;
            }
            return s + "}";
        }
    }
    return "null";
}

namespace ajustes {

std::string hex_de(unsigned c) {
    char buf[16];
    snprintf(buf, sizeof buf, "#%06x", c & 0xffffff);
    return buf;
}

unsigned color_de_hex(const std::string& s, unsigned si_no) {
    if (s.size() != 7 || s[0] != '#') return si_no;
    return (unsigned)strtoul(s.c_str() + 1, nullptr, 16);
}

void cargar(const std::wstring& carpeta_exe) {
    g_carpeta = carpeta_exe;
    HANDLE h = CreateFileW((carpeta_exe + L"\\ajustes.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string s;
    char buf[4096];
    DWORD leido = 0;
    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) s.append(buf, leido);
    CloseHandle(h);
    Json j = Json::parsear(s);
    Ajustes a;
    a.tema = j["tema"].str("dark");
    a.custom = paleta_de_json(j["custom"], Paleta());
    a.fondo = ancho(j["fondo"].str("whatsapp"));
    a.letra_lista = (float)j["letra_lista"].num(15);
    a.letra_chat = (float)j["letra_chat"].num(14.5);
    a.notificaciones = j["notificaciones"].bul(true);
    a.llamadas_web = j["llamadas_web"].bul(false);
    a.mensajes_por_chat = (int)j["mensajes_por_chat"].num(200);
    a.monitor_avisos = (int)j["monitor_avisos"].num(-1);
    a.esquina_avisos = (int)j["esquina_avisos"].num(0);
    a.segundos_aviso = (int)j["segundos_aviso"].num(6);
    a.volumen_video = (int)j["volumen_video"].num(100);
    a.velocidad = j["velocidad"].num(1.0);
    a.idioma_traduccion = ancho(j["idioma_traduccion"].str("English"));
    a.ventana_x = (int)j["ventana_x"].num(0);
    a.ventana_y = (int)j["ventana_y"].num(0);
    a.ventana_w = (int)j["ventana_w"].num(0);
    a.ventana_h = (int)j["ventana_h"].num(0);
    a.ventana_max = j["ventana_max"].bul(false);
    a.entrada = ancho(j["entrada"].str());
    a.salida = ancho(j["salida"].str());
    g_ajustes = a;
}

void guardar() {
    Json j;
    j.tipo = Json::Objeto;
    auto texto = [&](const char* k, const std::string& v) {
        Json x;
        x.tipo = Json::Texto;
        x.s = v;
        j.objeto[k] = x;
    };
    auto numero = [&](const char* k, double v) {
        Json x;
        x.tipo = Json::Numero;
        x.n = v;
        j.objeto[k] = x;
    };
    texto("tema", g_ajustes.tema);
    j.objeto["custom"] = paleta_a_json(g_ajustes.custom);
    texto("fondo", angosto(g_ajustes.fondo));
    numero("letra_lista", g_ajustes.letra_lista);
    numero("letra_chat", g_ajustes.letra_chat);
    Json b;
    b.tipo = Json::Booleano;
    b.b = g_ajustes.notificaciones;
    j.objeto["notificaciones"] = b;
    Json bl;
    bl.tipo = Json::Booleano;
    bl.b = g_ajustes.llamadas_web;
    j.objeto["llamadas_web"] = bl;
    numero("mensajes_por_chat", g_ajustes.mensajes_por_chat);
    numero("monitor_avisos", g_ajustes.monitor_avisos);
    numero("esquina_avisos", g_ajustes.esquina_avisos);
    numero("segundos_aviso", g_ajustes.segundos_aviso);
    numero("volumen_video", g_ajustes.volumen_video);
    numero("velocidad", g_ajustes.velocidad);
    texto("idioma_traduccion", angosto(g_ajustes.idioma_traduccion));
    numero("ventana_x", g_ajustes.ventana_x);
    numero("ventana_y", g_ajustes.ventana_y);
    numero("ventana_w", g_ajustes.ventana_w);
    numero("ventana_h", g_ajustes.ventana_h);
    Json bm;
    bm.tipo = Json::Booleano;
    bm.b = g_ajustes.ventana_max;
    j.objeto["ventana_max"] = bm;
    texto("entrada", angosto(g_ajustes.entrada));
    texto("salida", angosto(g_ajustes.salida));
    std::string s = serializar(j);
    HANDLE h = CreateFileW((g_carpeta + L"\\ajustes.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD escrito = 0;
    WriteFile(h, s.data(), (DWORD)s.size(), &escrito, nullptr);
    CloseHandle(h);
}

Ajustes& actual() { return g_ajustes; }

void cambiar(const std::function<void(Ajustes&)>& f) {
    f(g_ajustes);
    g_revision++;
    guardar();
}

unsigned revision() { return g_revision.load(); }

const Paleta& paleta() {
    // Se recalcula solo cuando cambia algo.
    static Paleta cacheada;
    static unsigned para = 0;
    if (para != g_revision.load()) {
        cacheada = preset(g_ajustes.tema);
        para = g_revision.load();
    }
    return cacheada;
}

Paleta paleta_de(const std::string& tema) { return preset(tema); }

const std::vector<std::pair<std::string, std::wstring>>& temas() {
    static const std::vector<std::pair<std::string, std::wstring>> lista = {
        {"dark", L"Dark"},   {"black", L"Black"}, {"light", L"Light"}, {"blue", L"Blue"},
        {"orange", L"Orange"}, {"green", L"Green"}, {"red", L"Red"},   {"custom", L"Custom"},
    };
    return lista;
}

std::wstring carpeta_fondos() { return g_carpeta + L"\\fondos"; }

std::vector<std::wstring> fondos_disponibles() {
    std::vector<std::wstring> r;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((carpeta_fondos() + L"\\*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return r;
    do {
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring n = d.cFileName;
        std::wstring e = n.size() > 4 ? n.substr(n.size() - 4) : L"";
        for (auto& c : e) c = towlower(c);
        std::wstring e5 = n.size() > 5 ? n.substr(n.size() - 5) : L"";
        for (auto& c : e5) c = towlower(c);
        if (e == L".jpg" || e == L".png" || e == L".gif" || e == L".bmp" || e5 == L".jpeg" || e5 == L".webp") r.push_back(n);
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return r;
}

}  // namespace ajustes
