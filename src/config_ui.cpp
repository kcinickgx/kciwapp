// Primera vez (sin servidor.json): una pantalla propia para elegir donde
// corre WhatsApp: en esta PC (el core local) o en un kciwapp-server (host,
// puerto y token). Escribe servidor.json y conecta.
#include "app.h"
#include "core.h"
#include "red.h"
#include "tema.h"

namespace {

constexpr float PANEL_W = 620.0f;
constexpr float TARJETA_H = 116.0f;
constexpr float CAMPO_H = 34.0f;

// Geometria de la pantalla, calculada del tamano de la ventana.
struct Geo {
    float x, y, w;          // panel
    float t1x, t2x, ty, tw; // tarjetas
    float fx, fw;           // campos (remoto)
    float hy, py, ky;       // host, puerto, token
    float gx, gw;           // boton Generate
    float bx, by, bw, bh;   // boton Connect
};

Geo geo(float W, float H, bool remoto) {
    Geo g;
    g.w = std::min(PANEL_W, W - 40);
    g.x = (W - g.w) / 2;
    float alto = 60 + 30 + TARJETA_H + 24 + (remoto ? 3 * (CAMPO_H + 14) + 10 : 0) + 24 + 40;
    g.y = std::max(20.0f, (H - alto) / 2);
    g.tw = (g.w - 16) / 2;
    g.t1x = g.x;
    g.t2x = g.x + g.tw + 16;
    g.ty = g.y + 90;
    g.fx = g.x + 110;
    g.fw = g.w - 110;
    g.hy = g.ty + TARJETA_H + 30;
    g.py = g.hy + CAMPO_H + 14;
    g.ky = g.py + CAMPO_H + 14;
    g.gw = 90;
    g.gx = g.x + g.w - g.gw;
    g.bw = 140;
    g.bh = 40;
    g.bx = g.x + g.w - g.bw;
    g.by = (remoto ? g.ky + CAMPO_H + 14 : g.ty + TARJETA_H + 30) + 10;
    return g;
}

std::string token_al_azar() { return core::token_nuevo(); }

}  // namespace

void App::empezar_configuracion() {
    config_pendiente = true;
    cfg_modo = 0;
    for (Campo* c : {&cfg_host, &cfg_puerto, &cfg_token}) {
        c->tamano = 14;
        c->alto_max = CAMPO_H;
        c->al_cambiar = [this] { pedir_dibujo(); };
        c->al_enviar = [this] { config_conectar(); };
    }
    cfg_host.indicio = L"192.168.1.10";
    cfg_puerto.indicio = L"8080";
    cfg_puerto.poner(L"8080");
    cfg_token.indicio = L"20+ characters (any)";
    cfg_error.clear();
    campo.foco = false;
    pedir_dibujo();
}

void App::dibujar_configuracion() {
    float W = g.ancho, H = g.alto;
    bool remoto = cfg_modo == 1;
    Geo q = geo(W, H, remoto);
    g.rect(0, 0, W, H, Color(BG_APP()));
    g.rect(0, 0, W, 6, Color(ACCENT()));
    std::wstring t = L"Welcome to kciwapp";
    g.renglon(t, q.x, q.y, 26, Color(TXT()), DWRITE_FONT_WEIGHT_LIGHT);
    g.renglon(L"Where does WhatsApp run?", q.x, q.y + 44, 14, Color(TXT_DIM()));

    auto tarjeta = [&](float x, bool elegida, const wchar_t* titulo, const wchar_t* l1, const wchar_t* l2, bool disponible) {
        g.rect_redondo(x, q.ty, q.tw, TARJETA_H, 10, Color(elegida ? BG_SEL() : BG_PANEL()));
        g.borde_redondo(x, q.ty, q.tw, TARJETA_H, 10, Color(elegida ? ACCENT() : BORDE()), elegida ? 2.0f : 1.0f);
        g.renglon(titulo, x + 16, q.ty + 14, 16, Color(disponible ? TXT() : TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(l1, x + 16, q.ty + 44, 12.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.tw - 32);
        g.renglon(l2, x + 16, q.ty + 64, 12.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.tw - 32);
        // El circulo de eleccion.
        float cx = x + q.tw - 22, cy = q.ty + 22;
        g.borde_redondo(cx - 8, cy - 8, 16, 16, 8, Color(elegida ? ACCENT() : TXT_DIM()), 1.5f);
        if (elegida) g.circulo(cx, cy, 4.5f, Color(ACCENT()));
    };
    bool hay_core = core::disponible(carpeta_exe());
    tarjeta(q.t1x, !remoto, L"This computer", L"WhatsApp runs right here.",
            hay_core ? L"Online only while kciwapp is open." : L"core\\kciwapp-core.exe is missing.", hay_core);
    tarjeta(q.t2x, remoto, L"A kciwapp server", L"Always on, on your LAN.",
            L"Needs its address and a token.", true);

    if (remoto) {
        auto campo_fila = [&](const wchar_t* etiqueta, Campo& c, float y, float w) {
            g.renglon(etiqueta, q.x, y + 9, 13, Color(TXT_DIM()));
            g.rect_redondo(q.fx, y, w, CAMPO_H, 8, Color(BG_CAMPO()));
            if (c.foco) g.borde_redondo(q.fx, y, w, CAMPO_H, 8, Color(ACCENT()), 1.0f);
            c.dibujar(g, q.fx + 10, y + 1, w - 20, CAMPO_H - 2, ahora);
        };
        campo_fila(L"Server", cfg_host, q.hy, q.fw);
        campo_fila(L"Port", cfg_puerto, q.py, 120);
        campo_fila(L"Token", cfg_token, q.ky, q.fw - q.gw - 10);
        g.rect_redondo(q.gx, q.ky + 2, q.gw, CAMPO_H - 4, 8, Color(BG_PANEL()));
        g.borde_redondo(q.gx, q.ky + 2, q.gw, CAMPO_H - 4, 8, Color(BORDE()), 1.0f);
        float gw = g.medir(L"Generate", 13);
        g.renglon(L"Generate", q.gx + (q.gw - gw) / 2, q.ky + 9, 13, Color(TXT()));
        g.renglon(L"A new token creates a new account on the server; use the same token on another PC to share it.",
                  q.x, q.ky + CAMPO_H + 4, 11.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.w);
    }
    // Connect
    bool puede = remoto ? true : hay_core;
    g.rect_redondo(q.bx, q.by, q.bw, q.bh, 8, Color(ACCENT(), puede ? 1.0f : 0.4f));
    float bw = g.medir(L"Connect", 15, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"Connect", q.bx + (q.bw - bw) / 2, q.by + 10, 15, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (!cfg_error.empty()) g.renglon(cfg_error, q.x, q.by + 12, 13, Color(0xf15c6d), DWRITE_FONT_WEIGHT_NORMAL, q.bx - q.x - 16);
}

bool App::click_configuracion(float x, float y, bool shift) {
    if (!config_pendiente) return false;
    bool remoto = cfg_modo == 1;
    Geo q = geo(g.ancho, g.alto, remoto);
    cfg_host.foco = cfg_puerto.foco = cfg_token.foco = false;
    if (y >= q.ty && y < q.ty + TARJETA_H) {
        if (x >= q.t1x && x < q.t1x + q.tw) cfg_modo = 0;
        else if (x >= q.t2x && x < q.t2x + q.tw) {
            cfg_modo = 1;
            cfg_host.foco = true;
        }
        cfg_error.clear();
        pedir_dibujo();
        return true;
    }
    if (remoto) {
        struct F {
            Campo* c;
            float y, w;
        } filas[] = {{&cfg_host, q.hy, q.fw}, {&cfg_puerto, q.py, 120}, {&cfg_token, q.ky, q.fw - q.gw - 10}};
        for (auto& f : filas)
            if (x >= q.fx && x < q.fx + f.w && y >= f.y && y < f.y + CAMPO_H) {
                f.c->foco = true;
                f.c->click(g, x, y, shift);
                pedir_dibujo();
                return true;
            }
        if (x >= q.gx && x < q.gx + q.gw && y >= q.ky && y < q.ky + CAMPO_H) {
            cfg_token.poner(ancho(token_al_azar()));
            cfg_token.foco = true;
            pedir_dibujo();
            return true;
        }
    }
    if (x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh) config_conectar();
    pedir_dibujo();
    return true;
}

bool App::clickeable_configuracion(float x, float y) const {
    if (!config_pendiente) return false;
    bool remoto = cfg_modo == 1;
    Geo q = geo(g.ancho, g.alto, remoto);
    if (y >= q.ty && y < q.ty + TARJETA_H && ((x >= q.t1x && x < q.t1x + q.tw) || (x >= q.t2x && x < q.t2x + q.tw))) return true;
    if (remoto && x >= q.gx && x < q.gx + q.gw && y >= q.ky && y < q.ky + CAMPO_H) return true;
    return x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh;
}

bool App::tecla_configuracion(WPARAM vk, bool shift, bool ctrl) {
    if (!config_pendiente) return false;
    Campo* orden[] = {&cfg_host, &cfg_puerto, &cfg_token};
    if (vk == VK_TAB && cfg_modo == 1) {
        int cual = -1;
        for (int i = 0; i < 3; i++)
            if (orden[i]->foco) cual = i;
        for (int i = 0; i < 3; i++) orden[i]->foco = false;
        int sig = cual < 0 ? 0 : (shift ? (cual + 2) % 3 : (cual + 1) % 3);
        orden[sig]->foco = true;
        orden[sig]->seleccionar_todo();
        pedir_dibujo();
        return true;
    }
    if (vk == VK_RETURN && (cfg_modo == 0 || !(cfg_host.foco || cfg_puerto.foco || cfg_token.foco))) {
        config_conectar();
        return true;
    }
    for (Campo* c : orden)
        if (c->foco && c->tecla(g, vk, shift, ctrl)) {
            pedir_dibujo();
            return true;
        }
    return true;
}

bool App::caracter_configuracion(wchar_t c) {
    if (!config_pendiente) return false;
    for (Campo* x : {&cfg_host, &cfg_puerto, &cfg_token})
        if (x->foco) {
            if (c == L'\t') return true;
            if (x->caracter(c)) pedir_dibujo();
            return true;
        }
    return true;
}

// Escribe servidor.json, levanta el core si es local, y arranca.
void App::config_conectar() {
    std::string host, token;
    int puerto = 0;
    if (cfg_modo == 0) {
        if (!core::disponible(carpeta_exe())) {
            cfg_error = L"core\\kciwapp-core.exe is missing";
            pedir_dibujo();
            return;
        }
        host = "127.0.0.1";
        puerto = 8477;
        token = core::token_nuevo();
    } else {
        host = angosto(cfg_host.texto);
        while (!host.empty() && host.back() == ' ') host.pop_back();
        while (!host.empty() && host.front() == ' ') host.erase(host.begin());
        puerto = _wtoi(cfg_puerto.texto.c_str());
        token = angosto(cfg_token.texto);
        if (host.empty()) {
            cfg_error = L"Server address is required";
            pedir_dibujo();
            return;
        }
        if (puerto <= 0 || puerto > 65535) {
            cfg_error = L"Port must be 1-65535";
            pedir_dibujo();
            return;
        }
        if (token.size() < 20) {
            cfg_error = L"Token must have at least 20 characters";
            pedir_dibujo();
            return;
        }
    }
    std::string s = "{\"host\": " + json_texto(host) + ", \"puerto\": " + std::to_string(puerto) + ", \"token\": " + json_texto(token) + "}\n";
    HANDLE h = CreateFileW((carpeta_exe() + L"\\servidor.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        cfg_error = L"Cannot write servidor.json";
        pedir_dibujo();
        return;
    }
    DWORD e = 0;
    WriteFile(h, s.data(), (DWORD)s.size(), &e, nullptr);
    CloseHandle(h);
    if (host == "127.0.0.1" && !core::iniciar(carpeta_exe(), puerto, token)) {
        cfg_error = L"Could not start the local core";
        pedir_dibujo();
        return;
    }
    red::configurar(ancho(host), puerto, token);
    config_pendiente = false;
    campo.foco = true;
    aviso_estado = L"Connecting...";
    cargar_chats();
    pedir_dibujo();
}
