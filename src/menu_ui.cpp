// Menu contextual propio (con los colores del tema e iconos de Segoe MDL2),
// en lugar del menu nativo de Windows.
#include "app.h"
#include "tema.h"

namespace {
constexpr float MENU_W = 220.0f;
constexpr float ITEM_H = 34.0f;
constexpr float SEP_H = 9.0f;
constexpr float PAD = 6.0f;

float alto_menu(const std::vector<ItemMenu>& items) {
    float h = PAD * 2;
    for (auto& it : items) h += it.separador ? SEP_H : ITEM_H;
    return h;
}
}  // namespace

void App::abrir_menu(std::vector<ItemMenu> items, float x, float y, std::function<void(int)> accion) {
    menu_items = std::move(items);
    menu_accion = std::move(accion);
    float h = alto_menu(menu_items);
    // Que entre en la ventana.
    menu_x = std::clamp(x, 4.0f, std::max(4.0f, g.ancho - MENU_W - 4));
    menu_y = y + h > g.alto - 4 ? std::max(4.0f, y - h) : y;
    menu_abierto = true;
    pedir_dibujo();
}

void App::cerrar_menu() {
    menu_abierto = false;
    menu_items.clear();
    pedir_dibujo();
}

void App::dibujar_menu() {
    if (!menu_abierto) return;
    float h = alto_menu(menu_items);
    g.rect_redondo(menu_x + 3, menu_y + 4, MENU_W, h, 8, Color(0x000000, 0.35f));
    g.rect_redondo(menu_x, menu_y, MENU_W, h, 8, Color(BG_PANEL()));
    g.borde_redondo(menu_x, menu_y, MENU_W, h, 8, Color(BORDE()), 1.0f);
    float y = menu_y + PAD;
    for (auto& it : menu_items) {
        if (it.separador) {
            g.linea(menu_x + 10, y + SEP_H / 2, menu_x + MENU_W - 10, y + SEP_H / 2, Color(BORDE()));
            y += SEP_H;
            continue;
        }
        bool encima = mouse_x >= menu_x && mouse_x < menu_x + MENU_W && mouse_y >= y && mouse_y < y + ITEM_H;
        if (encima && it.habilitado) g.rect_redondo(menu_x + 4, y, MENU_W - 8, ITEM_H, 6, Color(BG_CAMPO()));
        Color c(it.habilitado ? (it.peligroso ? 0xf15c6d : TXT()) : TXT_DIM());
        if (it.icono) g.renglon_fuente(L"Segoe MDL2 Assets", it.icono, menu_x + 16, y + 9, 15, Color(it.habilitado ? (it.peligroso ? 0xf15c6d : ACCENT()) : TXT_DIM()));
        g.renglon(it.texto, menu_x + 44, y + 8, 14, c);
        y += ITEM_H;
    }
}

// Devuelve si se comio el click (siempre que el menu este abierto).
bool App::click_menu(float x, float y) {
    if (!menu_abierto) return false;
    float h = alto_menu(menu_items);
    if (x < menu_x || x >= menu_x + MENU_W || y < menu_y || y >= menu_y + h) {
        cerrar_menu();
        return true;
    }
    float yy = menu_y + PAD;
    for (auto& it : menu_items) {
        float ih = it.separador ? SEP_H : ITEM_H;
        if (y >= yy && y < yy + ih) {
            if (!it.separador && it.habilitado) {
                int id = it.id;
                auto accion = menu_accion;
                cerrar_menu();
                if (accion) accion(id);
            }
            return true;
        }
        yy += ih;
    }
    return true;
}
