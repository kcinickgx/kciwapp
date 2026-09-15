// Selector de emojis: panel flotante anclado arriba-izquierda del pie, con
// buscador, pestañas de categoria y una grilla con scroll suave.
#include "app.h"
#include "emoji.h"
#include "tema.h"

namespace {

constexpr float PANEL_W = 380.0f, PANEL_H = 420.0f;
constexpr float PAD = 10.0f;
constexpr float BUSCADOR_H = 36.0f;
constexpr float TABS_H = 36.0f;
constexpr float GAP = 8.0f;
constexpr float CELDA = 40.0f;
constexpr int COLUMNAS = 9;

struct RectPanel {
    float x = 0, y = 0, w = 0, h = 0;
};

// Geometria del panel: la misma cuenta la usan dibujar_emojis y click_emojis.
RectPanel rect_panel(const App& a) {
    RectPanel p;
    p.w = PANEL_W;
    p.h = PANEL_H;
    p.x = a.x_conv() + 12.0f;
    p.y = a.g.alto - a.alto_pie - PANEL_H - 6.0f;
    return p;
}

void rect_buscador(const RectPanel& p, float& x, float& y, float& w, float& h) {
    x = p.x + PAD;
    y = p.y + PAD;
    w = p.w - 2 * PAD;
    h = BUSCADOR_H;
}

void rect_tabs(const RectPanel& p, float& x, float& y, float& w, float& h) {
    x = p.x + PAD;
    y = p.y + PAD + BUSCADOR_H + GAP;
    w = p.w - 2 * PAD;
    h = TABS_H;
}

void rect_grid(const RectPanel& p, float& x, float& y, float& w, float& h) {
    x = p.x + PAD;
    y = p.y + PAD + BUSCADOR_H + GAP + TABS_H + GAP;
    w = p.w - 2 * PAD;
    h = p.y + p.h - PAD - y;
}

int cuantas_pestanas() { return 1 + (int)emoji::categorias().size(); }

// El primer emoji de una categoria, para representarla en su pestaña.
std::wstring representante(int categoria) {
    for (const auto& e : emoji::todos())
        if (e.categoria == categoria) return e.simbolo;
    return L"";
}

// Que se muestra en la grilla: resultados de busqueda, recientes, o la
// categoria activa.
std::vector<std::wstring> contenido(const App& a) {
    std::vector<std::wstring> r;
    if (!a.emoji_buscador.texto.empty()) {
        for (const auto* e : emoji::buscar(a.emoji_buscador.texto)) r.push_back(e->simbolo);
        return r;
    }
    if (a.emoji_categoria == -1) return emoji::recientes();
    for (const auto& e : emoji::todos())
        if (e.categoria == a.emoji_categoria) r.push_back(e.simbolo);
    return r;
}

}  // namespace

void App::dibujar_emojis() {
    if (!emojis_abierto) return;
    RectPanel p = rect_panel(*this);

    // Sombra sutil, desplazada, y el panel encima.
    g.rect_redondo(p.x + 3, p.y + 4, p.w, p.h, 10, Color(0x000000, 0.3f));
    g.rect_redondo(p.x, p.y, p.w, p.h, 10, Color(BG_PANEL()));
    g.borde_redondo(p.x, p.y, p.w, p.h, 10, Color(BORDE()));

    // Buscador.
    float bx, by, bw, bh;
    rect_buscador(p, bx, by, bw, bh);
    if (emoji_buscador.indicio.empty()) {
        emoji_buscador.indicio = L"Search emoji";
        emoji_buscador.tamano = 13;
    }
    g.rect_redondo(bx, by, bw, bh, 8, Color(BG_CAMPO()));
    emoji_buscador.dibujar(g, bx, by, bw, bh, ahora);

    // Pestañas: reloj (recientes) + una por categoria.
    float tx, ty, tw, th;
    rect_tabs(p, tx, ty, tw, th);
    int cuantas = cuantas_pestanas();
    float ancho_tab = tw / cuantas;
    for (int i = 0; i < cuantas; i++) {
        int cat = i - 1;
        float cx = tx + i * ancho_tab;
        std::wstring simbolo = cat == -1 ? L"\U0001F550" : representante(cat);
        float sw = g.medir(simbolo, 18);
        g.renglon(simbolo, cx + (ancho_tab - sw) / 2, ty + 2, 18, Color(TXT()));
        if (cat == emoji_categoria)
            g.rect_redondo(cx + ancho_tab / 2 - 12, ty + th - 3, 24, 2, 1, Color(ACCENT()));
    }

    // Grilla, con scroll.
    float gx, gy, gw, gh;
    rect_grid(p, gx, gy, gw, gh);
    std::vector<std::wstring> items = contenido(*this);
    int filas = ((int)items.size() + COLUMNAS - 1) / COLUMNAS;
    emoji_scroll.max = std::max(0.0f, filas * CELDA - gh);
    emoji_scroll.limitar();

    g.recortar(gx, gy, gw, gh);
    if (items.empty()) {
        bool son_recientes = emoji_categoria == -1 && emoji_buscador.texto.empty();
        std::wstring t = son_recientes ? L"No recent emojis" : L"No emojis found";
        float tw2 = g.medir(t, 13);
        g.renglon(t, gx + (gw - tw2) / 2, gy + gh / 2 - 8, 13, Color(TXT_DIM()));
    } else {
        emoji_bajo_mouse = -1;
        for (int i = 0; i < (int)items.size(); i++) {
            int fila = i / COLUMNAS, col = i % COLUMNAS;
            float cx = gx + col * CELDA;
            float cy = gy + fila * CELDA - emoji_scroll.pos;
            if (cy + CELDA < gy || cy > gy + gh) continue;
            bool bajo_mouse = mouse_x >= cx && mouse_x < cx + CELDA && mouse_y >= cy && mouse_y < cy + CELDA;
            if (bajo_mouse) {
                emoji_bajo_mouse = i;
                g.rect_redondo(cx + 2, cy + 2, CELDA - 4, CELDA - 4, 8, Color(BG_CAMPO()));
            }
            float sw = g.medir(items[i], 24);
            g.renglon(items[i], cx + (CELDA - sw) / 2, cy + (CELDA - 24) / 2, 24, Color(TXT()));
        }
    }
    g.destapar();

    if (!emoji_scroll.quieto()) pedir_dibujo();
}

bool App::click_emojis(float x, float y) {
    if (!emojis_abierto) return false;
    RectPanel p = rect_panel(*this);
    if (x < p.x || x >= p.x + p.w || y < p.y || y >= p.y + p.h) {
        emojis_abierto = false;
        pedir_dibujo();
        return false;
    }

    float bx, by, bw, bh;
    rect_buscador(p, bx, by, bw, bh);
    if (y >= by && y < by + bh) {
        emoji_buscador.foco = true;
        campo.foco = false;
        emoji_buscador.click(g, x, y, false);
        pedir_dibujo();
        return true;
    }

    float tx, ty, tw, th;
    rect_tabs(p, tx, ty, tw, th);
    if (y >= ty && y < ty + th) {
        int cuantas = cuantas_pestanas();
        float ancho_tab = tw / cuantas;
        int i = std::clamp((int)((x - tx) / ancho_tab), 0, cuantas - 1);
        emoji_categoria = i - 1;
        emoji_scroll.ir(0, true);
        emoji_buscador.poner(L"");
        pedir_dibujo();
        return true;
    }

    float gx, gy, gw, gh;
    rect_grid(p, gx, gy, gw, gh);
    if (y >= gy && y < gy + gh && x >= gx && x < gx + gw) {
        std::vector<std::wstring> items = contenido(*this);
        int col = (int)((x - gx) / CELDA);
        int fila = (int)((y - gy + emoji_scroll.pos) / CELDA);
        int i = fila * COLUMNAS + col;
        if (col >= 0 && col < COLUMNAS && i >= 0 && i < (int)items.size()) elegir_emoji(items[i]);
        return true;
    }

    return true;
}

bool App::rueda_emojis(float x, float y, float delta) {
    if (!emojis_abierto) return false;
    RectPanel p = rect_panel(*this);
    if (x < p.x || x >= p.x + p.w || y < p.y || y >= p.y + p.h) return false;
    emoji_scroll.rodar(-delta / 120.0f * 3 * 40.0f);
    pedir_dibujo();
    return true;
}

void App::elegir_emoji(const std::wstring& simbolo) {
    campo.insertar(simbolo);
    emoji::usar(simbolo);
    campo.foco = true;
    emoji_buscador.foco = false;
    pedir_dibujo();
}
