// El selector de emojis, como el de WhatsApp: pestanas de categorias arriba,
// buscador, y una sola lista que scrollea con "Recent" (una fila) y despues
// cada categoria con su titulo. Las pestanas saltan a la seccion.
#include "app.h"
#include "emoji.h"
#include "tema.h"

namespace {

constexpr float PANEL_W = 420.0f, PANEL_H = 460.0f;
constexpr float PAD = 10.0f;
constexpr float TABS_H = 40.0f;
constexpr float BUSCADOR_H = 36.0f;
constexpr float GAP = 8.0f;
constexpr float CELDA = 40.0f;
constexpr float TITULO_H = 30.0f;
constexpr int COLUMNAS = 10;

struct Rect {
    float x, y, w, h;
    bool tiene(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

Rect rect_panel(const App& a) {
    return {a.x_conv() + 12, a.g.alto - a.alto_pie - PANEL_H - 6, PANEL_W, PANEL_H};
}
Rect rect_tabs(const Rect& p) { return {p.x + PAD, p.y + PAD, p.w - 2 * PAD, TABS_H}; }
Rect rect_buscador(const Rect& p) { return {p.x + PAD, p.y + PAD + TABS_H + GAP, p.w - 2 * PAD, BUSCADOR_H}; }
Rect rect_grilla(const Rect& p) {
    float y = p.y + PAD + TABS_H + GAP + BUSCADOR_H + GAP;
    return {p.x + PAD, y, p.w - 2 * PAD, p.y + p.h - PAD - y};
}

// Un renglon de la lista: titulo de seccion, o una fila de emojis.
struct Renglon {
    bool titulo;
    std::wstring texto;                   // el titulo
    std::vector<const wchar_t*> simbolos;  // o los emojis de la fila
    int seccion;                          // -1 recientes, 0.. categoria
};

// Arma la lista completa (o el resultado de la busqueda).
std::vector<Renglon> armar_lista(const std::wstring& busqueda, std::vector<std::wstring>& recientes_guardados) {
    std::vector<Renglon> r;
    auto fila = [&](std::vector<const wchar_t*>& acum, int seccion) {
        if (acum.empty()) return;
        r.push_back({false, L"", acum, seccion});
        acum.clear();
    };
    std::vector<const wchar_t*> acum;
    if (!busqueda.empty()) {
        for (const emoji::Emoji* e : emoji::buscar(busqueda)) {
            acum.push_back(e->simbolo.c_str());
            if ((int)acum.size() == COLUMNAS) fila(acum, 0);
        }
        fila(acum, 0);
        if (r.empty()) r.push_back({true, L"No emojis found", {}, 0});
        return r;
    }
    recientes_guardados = emoji::recientes();
    if (!recientes_guardados.empty()) {
        r.push_back({true, L"Recent", {}, -1});
        for (size_t i = 0; i < recientes_guardados.size() && i < (size_t)COLUMNAS; i++) acum.push_back(recientes_guardados[i].c_str());
        fila(acum, -1);
    }
    const auto& cats = emoji::categorias();
    for (int c = 0; c < (int)cats.size(); c++) {
        r.push_back({true, cats[c], {}, c});
        for (const emoji::Emoji& e : emoji::todos())
            if (e.categoria == c) {
                acum.push_back(e.simbolo.c_str());
                if ((int)acum.size() == COLUMNAS) fila(acum, c);
            }
        fila(acum, c);
    }
    return r;
}

float alto_de(const Renglon& r) { return r.titulo ? TITULO_H : CELDA; }

// Un emoji representativo por categoria, para las pestanas.
const wchar_t* icono_de(int categoria) {
    for (const emoji::Emoji& e : emoji::todos())
        if (e.categoria == categoria) return e.simbolo.c_str();
    return L"?";
}

}  // namespace

void App::dibujar_emojis() {
    if (!emojis_abierto) return;
    if (emoji_buscador.indicio.empty()) {
        emoji_buscador.indicio = L"Search emoji";
        emoji_buscador.tamano = 13;
        emoji_buscador.alto_max = BUSCADOR_H;
    }
    Rect p = rect_panel(*this);
    g.rect_redondo(p.x + 3, p.y + 4, p.w, p.h, 10, Color(0x000000, 0.3f));
    g.rect_redondo(p.x, p.y, p.w, p.h, 10, Color(BG_PANEL()));
    g.borde_redondo(p.x, p.y, p.w, p.h, 10, Color(BORDE()), 1.0f);

    // Pestanas: reloj + una por categoria; la activa es la seccion que se ve.
    Rect t = rect_tabs(p);
    const auto& cats = emoji::categorias();
    int pestanas = (int)cats.size() + 1;
    float tw = t.w / pestanas;
    for (int k = 0; k < pestanas; k++) {
        int seccion = k - 1;
        const wchar_t* s = seccion < 0 ? L"\U0001F550" : icono_de(seccion);
        float sx = t.x + k * tw + (tw - 22) / 2;
        bool activa = seccion == emoji_categoria;
        g.renglon(s, sx, t.y + 6, 20, Color(activa ? TXT() : TXT_DIM()));
        if (activa) g.rect_redondo(t.x + k * tw + tw / 2 - 12, t.y + t.h - 4, 24, 3, 1.5f, Color(ACCENT()));
    }

    Rect b = rect_buscador(p);
    g.rect_redondo(b.x, b.y, b.w, b.h, 8, Color(BG_CAMPO()));
    emoji_buscador.dibujar(g, b.x, b.y, b.w, b.h, ahora);

    // La lista.
    Rect gr = rect_grilla(p);
    std::vector<std::wstring> recientes;
    std::vector<Renglon> lista = armar_lista(emoji_buscador.texto, recientes);
    float total = 0;
    for (auto& r : lista) total += alto_de(r);
    emoji_scroll.max = std::max(0.0, (double)total - gr.h);
    emoji_scroll.limitar();
    if (!emoji_scroll.quieto()) pedir_dibujo();
    float celda = gr.w / COLUMNAS;
    g.recortar(gr.x, gr.y, gr.w, gr.h);
    float y = gr.y - (float)emoji_scroll.pos;
    emoji_bajo_mouse = -1;
    int seccion_visible = -2;
    for (auto& r : lista) {
        float h = alto_de(r);
        if (y + h >= gr.y && y <= gr.y + gr.h) {
            if (seccion_visible == -2 && y + h > gr.y + 4) seccion_visible = r.seccion;
            if (r.titulo) {
                g.renglon(r.texto, gr.x + 4, y + 8, 13, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            } else {
                for (size_t k = 0; k < r.simbolos.size(); k++) {
                    float cx = gr.x + k * celda;
                    if (mouse_x >= cx && mouse_x < cx + celda && mouse_y >= std::max(y, gr.y) && mouse_y < std::min(y + h, gr.y + gr.h)) {
                        g.rect_redondo(cx + 2, y + 2, celda - 4, h - 4, 6, Color(BG_CAMPO()));
                        emoji_bajo_mouse = 1;
                    }
                    g.renglon(r.simbolos[k], cx + (celda - 26) / 2, y + 6, 24, Color(TXT()));
                }
            }
        }
        y += h;
        if (y > gr.y + gr.h) break;
    }
    g.destapar();
    if (emoji_buscador.texto.empty() && seccion_visible != -2) emoji_categoria = seccion_visible;
}

bool App::click_emojis(float x, float y) {
    if (!emojis_abierto) return false;
    Rect p = rect_panel(*this);
    if (!p.tiene(x, y)) {
        emojis_abierto = false;
        emoji_buscador.foco = false;
        pedir_dibujo();
        return false;
    }
    Rect b = rect_buscador(p);
    if (b.tiene(x, y)) {
        emoji_buscador.foco = true;
        campo.foco = false;
        emoji_buscador.click(g, x, y, false);
        pedir_dibujo();
        return true;
    }
    Rect t = rect_tabs(p);
    if (t.tiene(x, y)) {
        // Saltar a la seccion.
        int pestanas = (int)emoji::categorias().size() + 1;
        int k = (int)((x - t.x) / (t.w / pestanas));
        int seccion = std::clamp(k, 0, pestanas - 1) - 1;
        emoji_buscador.poner(L"");
        std::vector<std::wstring> recientes;
        std::vector<Renglon> lista = armar_lista(L"", recientes);
        float yy = 0;
        for (auto& r : lista) {
            if (r.titulo && r.seccion == seccion) break;
            yy += alto_de(r);
        }
        emoji_scroll.ir(yy, false);
        emoji_categoria = seccion;
        pedir_dibujo();
        return true;
    }
    Rect gr = rect_grilla(p);
    if (gr.tiene(x, y)) {
        std::vector<std::wstring> recientes;
        std::vector<Renglon> lista = armar_lista(emoji_buscador.texto, recientes);
        float yy = gr.y - (float)emoji_scroll.pos;
        float celda = gr.w / COLUMNAS;
        for (auto& r : lista) {
            float h = alto_de(r);
            if (y >= yy && y < yy + h && !r.titulo) {
                int k = (int)((x - gr.x) / celda);
                if (k >= 0 && k < (int)r.simbolos.size()) elegir_emoji(r.simbolos[k]);
                return true;
            }
            yy += h;
        }
    }
    return true;
}

bool App::rueda_emojis(float x, float y, float delta) {
    if (!emojis_abierto) return false;
    Rect p = rect_panel(*this);
    if (!p.tiene(x, y)) return false;
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
