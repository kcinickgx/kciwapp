// Las pantallas de cuentas: la de configuracion (una cuenta nueva: donde
// corre WhatsApp, y el token; si es un kciwapp-server, host y puerto) y el
// selector de cuenta al arrancar cuando hay mas de una. Tambien el menu de
// cuentas de la cabecera (cambiar / agregar).
#include "app.h"
#include "core.h"
#include "cuentas.h"
#include "red.h"
#include "tema.h"

namespace {

constexpr float PANEL_W = 620.0f;
constexpr float TARJETA_H = 116.0f;
constexpr float CAMPO_H = 34.0f;
constexpr float FILA_CUENTA_H = 64.0f;

// Geometria de la pantalla de configuracion, calculada del tamano de la ventana.
struct Geo {
    float x, y, w;          // panel
    float t1x, t2x, ty, tw; // tarjetas
    float fx, fw;           // campos
    float hy, ky;           // fila server+puerto, token
    float hw, plx, px, pw;  // ancho del host; etiqueta y campo del puerto
    float gx, gw;           // boton Generate
    float bx, by, bw, bh;   // boton Connect
    float cx, cw;           // boton Cancel
};

Geo geo(float W, float H, bool remoto) {
    Geo g;
    g.w = std::min(PANEL_W, W - 40);
    g.x = (W - g.w) / 2;
    float alto = 60 + 30 + TARJETA_H + 24 + 2 * (CAMPO_H + 14) + 10 + 24 + 40;
    g.y = std::max(20.0f, (H - alto) / 2);
    g.tw = (g.w - 16) / 2;
    g.t1x = g.x;
    g.t2x = g.x + g.tw + 16;
    g.ty = g.y + 90;
    g.fx = g.x + 110;
    g.fw = g.w - 110;
    g.hy = g.ty + TARJETA_H + 30;
    g.ky = g.hy + CAMPO_H + 14;
    g.pw = 100;
    g.px = g.x + g.w - g.pw;
    g.plx = g.px - 48;
    g.hw = g.plx - 12 - g.fx;
    g.gw = 90;
    g.gx = g.x + g.w - g.gw;
    g.bw = 140;
    g.bh = 40;
    g.bx = g.x + g.w - g.bw;
    g.by = g.ky + CAMPO_H + 14 + 10;
    g.cw = g.bw;
    g.cx = g.bx - g.cw - 12;
    return g;
}

// Geometria del selector de cuentas.
struct GeoSel {
    float x, y, w;         // panel
    float ly;              // primera fila
    float bx, by, bw, bh;  // boton Add another account
};

GeoSel geo_sel(float W, float H, int n) {
    GeoSel g;
    g.w = std::min(520.0f, W - 40);
    g.x = (W - g.w) / 2;
    float alto = 90 + n * (FILA_CUENTA_H + 10) + 16 + 40;
    g.y = std::max(20.0f, (H - alto) / 2);
    g.ly = g.y + 90;
    g.bw = 200;
    g.bh = 40;
    g.bx = g.x + g.w - g.bw;
    g.by = g.ly + n * (FILA_CUENTA_H + 10) + 16;
    return g;
}

// Vuelve de la pantalla de configuracion: al chat si ya habia cuenta
// abierta, o al selector.
void cancelar_configuracion(App& a) {
    a.config_pendiente = false;
    a.cfg_error.clear();
    if (red::configurado()) a.campo.foco = true;
    else a.empezar_selector();
    a.pedir_dibujo();
}

}  // namespace

// ---------------------------------------------------------------- configuracion

void App::empezar_configuracion() {
    config_pendiente = true;
    selector_pendiente = false;
    cfg_cancelable = red::configurado() || !cuentas::lista().empty();
    cfg_modo = core::disponible(carpeta_exe()) ? 0 : 1;
    for (Campo* c : {&cfg_host, &cfg_puerto, &cfg_token}) {
        c->tamano = 14;
        c->alto_max = CAMPO_H;
        c->al_cambiar = [this] { pedir_dibujo(); };
        c->al_enviar = [this] { config_conectar(); };
    }
    cfg_host.indicio = L"192.168.1.10";
    cfg_host.poner(L"");
    cfg_puerto.indicio = L"8080";
    cfg_puerto.poner(L"8080");
    cfg_token.indicio = L"20+ characters (any)";
    // Un token nuevo ya puesto: para una cuenta nueva es lo que va; para
    // entrar a una que ya existe en el server se pega el de esa.
    cfg_token.poner(ancho(core::token_nuevo()));
    cfg_token.foco = cfg_modo == 0;
    cfg_host.foco = cfg_modo == 1;
    cfg_puerto.foco = false;
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
    bool primera = cuentas::lista().empty();
    g.renglon(primera ? L"Welcome to kciwapp" : L"Add an account", q.x, q.y, 26, Color(TXT()), DWRITE_FONT_WEIGHT_LIGHT);
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

    auto campo_fila = [&](const wchar_t* etiqueta, float ex, Campo& c, float x, float y, float w) {
        g.renglon(etiqueta, ex, y + 9, 13, Color(TXT_DIM()));
        g.rect_redondo(x, y, w, CAMPO_H, 8, Color(BG_CAMPO()));
        if (c.foco) g.borde_redondo(x, y, w, CAMPO_H, 8, Color(ACCENT()), 1.0f);
        c.dibujar(g, x + 10, y + 1, w - 20, CAMPO_H - 2, ahora);
    };
    // Server y puerto en una fila; con el core local van fijos y apagados.
    auto campo_fijo = [&](const wchar_t* etiqueta, float ex, const std::wstring& valor, float x, float y, float w) {
        g.renglon(etiqueta, ex, y + 9, 13, Color(TXT_DIM()));
        g.rect_redondo(x, y, w, CAMPO_H, 8, Color(BG_CAMPO(), 0.5f));
        g.renglon(valor, x + 10, y + 9, 14, Color(TXT_DIM(), 0.6f));
    };
    if (remoto) {
        campo_fila(L"Server", q.x, cfg_host, q.fx, q.hy, q.hw);
        campo_fila(L"Port", q.plx, cfg_puerto, q.px, q.hy, q.pw);
    } else {
        campo_fijo(L"Server", q.x, L"127.0.0.1", q.fx, q.hy, q.hw);
        campo_fijo(L"Port", q.plx, std::to_wstring(cuentas::puerto_local_libre()), q.px, q.hy, q.pw);
    }
    campo_fila(L"Token", q.x, cfg_token, q.fx, q.ky, q.fw - q.gw - 10);
    g.rect_redondo(q.gx, q.ky + 2, q.gw, CAMPO_H - 4, 8, Color(BG_PANEL()));
    g.borde_redondo(q.gx, q.ky + 2, q.gw, CAMPO_H - 4, 8, Color(BORDE()), 1.0f);
    float gw = g.medir(L"Generate", 13);
    g.renglon(L"Generate", q.gx + (q.gw - gw) / 2, q.ky + 9, 13, Color(TXT()));
    g.renglon(remoto ? L"A new token creates a new account on the server; use the same token on another PC to share it."
                     : L"The token protects the local core; keep it if you copy this folder elsewhere.",
              q.x, q.ky + CAMPO_H + 4, 11.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.w);
    // Connect (y Cancel si hay a donde volver)
    bool puede = remoto ? true : hay_core;
    g.rect_redondo(q.bx, q.by, q.bw, q.bh, 8, Color(ACCENT(), puede ? 1.0f : 0.4f));
    float bw = g.medir(L"Connect", 15, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"Connect", q.bx + (q.bw - bw) / 2, q.by + 10, 15, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (cfg_cancelable) {
        g.borde_redondo(q.cx, q.by, q.cw, q.bh, 8, Color(BORDE()), 1.0f);
        float cw = g.medir(L"Cancel", 14);
        g.renglon(L"Cancel", q.cx + (q.cw - cw) / 2, q.by + 11, 14, Color(TXT()));
    }
    if (!cfg_error.empty()) g.renglon(cfg_error, q.x, q.by + 12, 13, Color(0xf15c6d), DWRITE_FONT_WEIGHT_NORMAL, (cfg_cancelable ? q.cx : q.bx) - q.x - 16);
}

bool App::click_configuracion(float x, float y, bool shift) {
    if (!config_pendiente) return false;
    bool remoto = cfg_modo == 1;
    Geo q = geo(g.ancho, g.alto, remoto);
    cfg_host.foco = cfg_puerto.foco = cfg_token.foco = false;
    if (y >= q.ty && y < q.ty + TARJETA_H) {
        if (x >= q.t1x && x < q.t1x + q.tw) {
            cfg_modo = 0;
            cfg_token.foco = true;
        } else if (x >= q.t2x && x < q.t2x + q.tw) {
            cfg_modo = 1;
            cfg_host.foco = true;
        }
        cfg_error.clear();
        pedir_dibujo();
        return true;
    }
    struct F {
        Campo* c;
        float x, y, w;
        bool activo;
    } filas[] = {{&cfg_host, q.fx, q.hy, q.hw, remoto}, {&cfg_puerto, q.px, q.hy, q.pw, remoto}, {&cfg_token, q.fx, q.ky, q.fw - q.gw - 10, true}};
    for (auto& f : filas)
        if (f.activo && x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + CAMPO_H) {
            f.c->foco = true;
            f.c->click(g, x, y, shift);
            pedir_dibujo();
            return true;
        }
    if (x >= q.gx && x < q.gx + q.gw && y >= q.ky && y < q.ky + CAMPO_H) {
        cfg_token.poner(ancho(core::token_nuevo()));
        cfg_token.foco = true;
        pedir_dibujo();
        return true;
    }
    if (x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh) config_conectar();
    else if (cfg_cancelable && x >= q.cx && x < q.cx + q.cw && y >= q.by && y < q.by + q.bh) cancelar_configuracion(*this);
    pedir_dibujo();
    return true;
}

bool App::clickeable_configuracion(float x, float y) const {
    if (!config_pendiente) return false;
    bool remoto = cfg_modo == 1;
    Geo q = geo(g.ancho, g.alto, remoto);
    if (y >= q.ty && y < q.ty + TARJETA_H && ((x >= q.t1x && x < q.t1x + q.tw) || (x >= q.t2x && x < q.t2x + q.tw))) return true;
    if (x >= q.gx && x < q.gx + q.gw && y >= q.ky && y < q.ky + CAMPO_H) return true;
    if (cfg_cancelable && x >= q.cx && x < q.cx + q.cw && y >= q.by && y < q.by + q.bh) return true;
    return x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh;
}

bool App::tecla_configuracion(WPARAM vk, bool shift, bool ctrl) {
    if (!config_pendiente) return false;
    // El orden del Tab: los campos visibles.
    Campo* orden[3];
    int n = 0;
    if (cfg_modo == 1) {
        orden[n++] = &cfg_host;
        orden[n++] = &cfg_puerto;
    }
    orden[n++] = &cfg_token;
    if (vk == VK_ESCAPE && cfg_cancelable) {
        cancelar_configuracion(*this);
        return true;
    }
    if (vk == VK_TAB) {
        int cual = -1;
        for (int i = 0; i < n; i++)
            if (orden[i]->foco) cual = i;
        for (int i = 0; i < n; i++) orden[i]->foco = false;
        int sig = cual < 0 ? 0 : (shift ? (cual + n - 1) % n : (cual + 1) % n);
        orden[sig]->foco = true;
        orden[sig]->seleccionar_todo();
        pedir_dibujo();
        return true;
    }
    if (vk == VK_RETURN && !(cfg_host.foco || cfg_puerto.foco || cfg_token.foco)) {
        config_conectar();
        return true;
    }
    for (int i = 0; i < n; i++)
        if (orden[i]->foco && orden[i]->tecla(g, vk, shift, ctrl)) {
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

// Guarda la cuenta nueva en cuentas.json y entra: directo si todavia no hay
// nada abierto, o relanzando el cliente con esa cuenta si ya hay otra.
void App::config_conectar() {
    Cuenta c;
    c.token = angosto(cfg_token.texto);
    while (!c.token.empty() && c.token.back() == ' ') c.token.pop_back();
    while (!c.token.empty() && c.token.front() == ' ') c.token.erase(c.token.begin());
    if (cfg_modo == 0) {
        if (!core::disponible(carpeta_exe())) {
            cfg_error = L"core\\kciwapp-core.exe is missing";
            pedir_dibujo();
            return;
        }
        c.host = "127.0.0.1";
    } else {
        c.host = angosto(cfg_host.texto);
        while (!c.host.empty() && c.host.back() == ' ') c.host.pop_back();
        while (!c.host.empty() && c.host.front() == ' ') c.host.erase(c.host.begin());
        if (c.host == "localhost") c.host = "127.0.0.1";
        c.puerto = _wtoi(cfg_puerto.texto.c_str());
        if (c.host.empty()) {
            cfg_error = L"Server address is required";
            pedir_dibujo();
            return;
        }
        if (c.puerto <= 0 || c.puerto > 65535) {
            cfg_error = L"Port must be 1-65535";
            pedir_dibujo();
            return;
        }
    }
    if (c.token.size() < 20) {
        cfg_error = L"Token must have at least 20 characters";
        pedir_dibujo();
        return;
    }
    for (const Cuenta& o : cuentas::lista())
        if (o.host == c.host && o.token == c.token) {
            cfg_error = L"That account is already here";
            pedir_dibujo();
            return;
        }
    int i = cuentas::agregar(c);
    if (red::configurado()) {
        cambiar_cuenta(i);
        return;
    }
    cuentas::elegir(i);
    if (!conectar_cuenta()) {
        cfg_error = L"Could not start the local core";
        pedir_dibujo();
    }
}

// ---------------------------------------------------------------- selector

void App::empezar_selector() {
    selector_pendiente = true;
    config_pendiente = false;
    campo.foco = false;
    pedir_dibujo();
}

void App::dibujar_selector() {
    float W = g.ancho, H = g.alto;
    const auto& lc = cuentas::lista();
    GeoSel q = geo_sel(W, H, (int)lc.size());
    g.rect(0, 0, W, H, Color(BG_APP()));
    g.rect(0, 0, W, 6, Color(ACCENT()));
    g.renglon(L"Welcome back", q.x, q.y, 26, Color(TXT()), DWRITE_FONT_WEIGHT_LIGHT);
    g.renglon(L"Which account?", q.x, q.y + 44, 14, Color(TXT_DIM()));
    for (size_t i = 0; i < lc.size(); i++) {
        float y = q.ly + i * (FILA_CUENTA_H + 10);
        bool hover = false;
        g.rect_redondo(q.x, y, q.w, FILA_CUENTA_H, 10, Color(hover ? BG_SEL() : BG_PANEL()));
        g.borde_redondo(q.x, y, q.w, FILA_CUENTA_H, 10, Color(hover ? ACCENT() : BORDE()), 1.0f);
        // La inicial en un circulo, como en la lista de chats.
        std::wstring et = cuentas::etiqueta(lc[i]);
        g.circulo(q.x + 32, y + FILA_CUENTA_H / 2, 20, color_de_nombre(et));
        std::wstring ini = et.substr(0, 1);
        float iw = g.medir(ini, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(ini, q.x + 32 - iw / 2, y + FILA_CUENTA_H / 2 - 11, 17, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(et, q.x + 66, y + 14, 15, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD, q.w - 90);
        g.renglon(cuentas::detalle(lc[i]), q.x + 66, y + 36, 12.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.w - 90);
        g.renglon_fuente(L"Segoe MDL2 Assets", L"", q.x + q.w - 30, y + FILA_CUENTA_H / 2 - 7, 12, Color(TXT_DIM()));
    }
    g.borde_redondo(q.bx, q.by, q.bw, q.bh, 8, Color(BORDE()), 1.0f);
    float bw = g.medir(L"Add another account", 14);
    g.renglon(L"Add another account", q.bx + (q.bw - bw) / 2, q.by + 11, 14, Color(TXT()));
}

bool App::click_selector(float x, float y) {
    if (!selector_pendiente) return false;
    const auto& lc = cuentas::lista();
    GeoSel q = geo_sel(g.ancho, g.alto, (int)lc.size());
    for (size_t i = 0; i < lc.size(); i++) {
        float fy = q.ly + i * (FILA_CUENTA_H + 10);
        if (x >= q.x && x < q.x + q.w && y >= fy && y < fy + FILA_CUENTA_H) {
            cuentas::elegir((int)i);
            if (!conectar_cuenta()) {
                cuentas::elegir(-1);
                MessageBoxW(hwnd, L"core\\kciwapp-core.exe is missing", L"kciwapp", MB_ICONERROR);
            }
            return true;
        }
    }
    if (x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh) empezar_configuracion();
    pedir_dibujo();
    return true;
}

bool App::clickeable_selector(float x, float y) const {
    if (!selector_pendiente) return false;
    const auto& lc = cuentas::lista();
    GeoSel q = geo_sel(g.ancho, g.alto, (int)lc.size());
    if (x >= q.x && x < q.x + q.w && y >= q.ly && y < q.ly + lc.size() * (FILA_CUENTA_H + 10)) {
        float rel = fmodf(y - q.ly, FILA_CUENTA_H + 10);
        return rel < FILA_CUENTA_H;
    }
    return x >= q.bx && x < q.bx + q.bw && y >= q.by && y < q.by + q.bh;
}

// ---------------------------------------------------------------- menu

// El menu de los tres puntos de la cabecera: las cuentas (la abierta con
// tilde; otra la abre en su lugar) y agregar una.
void App::menu_cuentas(float x, float y) {
    const auto& lc = cuentas::lista();
    std::vector<ItemMenu> items;
    for (size_t i = 0; i < lc.size(); i++) {
        ItemMenu it{cuentas::etiqueta(lc[i]), (int)i + 1, (int)i == cuentas::activa() ? L"" : nullptr};
        items.push_back(it);
    }
    if (!items.empty()) items.push_back({L"", 0, nullptr, true});
    items.push_back({L"Add account", 1000, L""});
    abrir_menu(std::move(items), x, y, [this](int id) {
        if (id == 1000) empezar_configuracion();
        else if (id >= 1 && id - 1 != cuentas::activa()) cambiar_cuenta(id - 1);
        pedir_dibujo();
    });
}
