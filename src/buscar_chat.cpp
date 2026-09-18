// Busqueda dentro del chat abierto (Ctrl+F o la lupa de la cabecera): el
// nombre del chat deja lugar a un campo, los resultados reemplazan a la
// conversacion mientras no se elige ninguno, y al elegir uno se salta al
// mensaje y quedan el contador y las flechas para moverse entre coincidencias.
#include <windowsx.h>

#include "app.h"
#include "red.h"
#include "tema.h"

namespace {
constexpr float ALTO_CAMPO = 36.0f;
constexpr int PAGINA = 1000000;  // todas de una, sin paginar
// Ancho reservado a la derecha del campo para "n/N" y las flechas.
constexpr float NAVEGADOR_W = 160.0f;
}  // namespace

void App::abrir_busqueda_chat() {
    if (chat_actual.empty()) return;
    if (buscador_chat.indicio.empty()) {
        buscador_chat.indicio = L"Search this chat";
        buscador_chat.tamano = 14;
        buscador_chat.alto_max = ALTO_CAMPO;
        buscador_chat.al_enviar = [this] { buscar_en_chat(); };
        buscador_chat.al_escapar = [this] { cerrar_busqueda_chat(); };
        buscador_chat.al_cambiar = [this] {
            pedir_dibujo();
            // Como la de la lista: espera a que dejes de escribir.
            SetTimer(hwnd, 5, 250, nullptr);
        };
    }
    busca_chat_abierta = true;
    buscador_chat.foco = true;
    campo.foco = false;
    buscador.foco = false;
    buscador_chat.seleccionar_todo();
    pedir_dibujo();
}

void App::cerrar_busqueda_chat() {
    if (!busca_chat_abierta) return;
    KillTimer(hwnd, 5);
    busca_chat_abierta = false;
    buscador_chat.foco = false;
    buscador_chat.poner(L"");
    ultima_busqueda_chat.clear();
    res_chat.clear();
    elegido_chat = -1;
    res_chat_completa = false;
    res_chat_buscando = false;
    scroll_res_chat = Desplazable();
    resaltado_id.clear();
    campo.foco = true;
    pedir_dibujo();
}

// Nueva busqueda con lo que hay escrito (si cambio).
void App::buscar_en_chat() {
    std::wstring q = buscador_chat.texto;
    while (!q.empty() && q.back() == L' ') q.pop_back();
    if (q == ultima_busqueda_chat) return;
    ultima_busqueda_chat = q;
    res_chat.clear();
    elegido_chat = -1;
    res_chat_completa = false;
    res_chat_buscando = false;
    scroll_res_chat.ir(0, true);
    if (q.size() < 2) {
        pedir_dibujo();
        return;
    }
    buscar_mas_chat();
}

// Siguiente pagina (mas viejos que el ultimo resultado que ya tenemos).
void App::buscar_mas_chat() {
    std::wstring q = ultima_busqueda_chat;
    if (q.size() < 2 || res_chat_completa || res_chat_buscando || chat_actual.empty()) return;
    res_chat_buscando = true;
    std::string qq = angosto(q);
    std::wstring url = L"/buscar?limite=" + std::to_wstring(PAGINA) + L"&chat=" + ancho(chat_actual) + L"&q=";
    for (unsigned char c : qq) {
        if (isalnum(c)) url += (wchar_t)c;
        else {
            wchar_t buf[8];
            swprintf(buf, 8, L"%%%02X", c);
            url += buf;
        }
    }
    if (!res_chat.empty()) url += L"&antes=" + std::to_wstring(res_chat.back().ts);
    std::string chat = chat_actual;
    red::en_fondo([this, url, q, chat] {
        Respuesta r = red::obtener(url);
        Json j = Json::parsear(r.cuerpo);
        red::en_ui([this, j, q, chat] {
            if (q != ultima_busqueda_chat || chat != chat_actual || !busca_chat_abierta) return;
            res_chat_buscando = false;
            for (size_t i = 0; i < j.largo(); i++) res_chat.push_back(Mensaje::de_json(j[i]));
            if ((int)j.largo() < PAGINA) res_chat_completa = true;
            pedir_dibujo();
        });
    });
}

// Salta a la coincidencia `i` (0 = la mas nueva).
void App::ir_a_coincidencia(int i) {
    if (i < 0 || i >= (int)res_chat.size()) return;
    elegido_chat = i;
    const Mensaje& m = res_chat[i];
    ir_a_mensaje(m.chat, m.id, m.ts);
    // Cerca del final de lo que tenemos: pedir mas para que las flechas sigan.
    if (i + 5 >= (int)res_chat.size()) buscar_mas_chat();
    pedir_dibujo();
}

// dir = +1 hacia atras en el tiempo (mas viejo), -1 hacia adelante.
void App::mover_coincidencia(int dir) {
    if (elegido_chat < 0) {
        if (!res_chat.empty()) ir_a_coincidencia(0);
        return;
    }
    ir_a_coincidencia(elegido_chat + dir);
}

// La cabecera con la busqueda abierta: el campo, el contador y las flechas.
// Se llama desde dibujar_cabecera despues del avatar; `tx` es donde empezaria
// el nombre.
void App::dibujar_busqueda_cabecera(float tx, float x, float W, float H) {
    float cx = tx, cy = (H - ALTO_CAMPO) / 2, cw = x + W - 56 - cx;
    g.rect_redondo(cx, cy, cw, ALTO_CAMPO, 8, Color(BG_CAMPO()));
    g.lupa(cx + 17, cy + 17, 6, Color(TXT_DIM()));
    bool nav = elegido_chat >= 0 && !res_chat.empty();
    float fw = cw - 34 - (nav ? NAVEGADOR_W : 0);
    buscador_chat.dibujar(g, cx + 30, cy + 2, fw, ALTO_CAMPO - 4, ahora);
    if (nav) {
        int actual = std::min(elegido_chat, (int)res_chat.size() - 1);
        std::wstring cuenta = std::to_wstring(actual + 1) + L"/" + std::to_wstring(res_chat.size()) + (res_chat_completa ? L"" : L"+");
        float nx = cx + cw - NAVEGADOR_W;
        float cw2 = g.medir(cuenta, 12.5f);
        g.renglon(cuenta, nx + 100 - cw2, cy + 10, 12.5f, Color(TXT_DIM()));
        bool hay_viejo = actual + 1 < (int)res_chat.size() || !res_chat_completa;
        bool hay_nuevo = actual > 0;
        // Arriba = mas viejo (como en la lista, que va del mas nuevo al mas viejo).
        g.renglon_fuente(L"Segoe MDL2 Assets", L"\uE70E", nx + 112, cy + 11, 13, Color(hay_viejo ? TXT() : TXT_DIM(), hay_viejo ? 1.0f : 0.45f));
        g.renglon_fuente(L"Segoe MDL2 Assets", L"\uE70D", nx + 138, cy + 11, 13, Color(hay_nuevo ? TXT() : TXT_DIM(), hay_nuevo ? 1.0f : 0.45f));
    }
    // La cruz para cerrar, donde estaba la lupa.
    g.renglon(L"✕", x + W - 42, 20, 18, Color(TXT_DIM()));
}

// El panel de resultados en lugar de la conversacion.
void App::dibujar_resultados_chat() {
    float x = x_conv(), W = w_conv(), top = alto_cabecera(), bottom = g.alto - alto_pie, H = bottom - top;
    g.rect(x, top, W, H, Color(BG_APP()));
    float FILA = fila_h();
    if (res_chat.empty()) {
        std::wstring t = res_chat_buscando ? L"Searching..." : (ultima_busqueda_chat.size() < 2 ? L"Type to search this chat" : L"No results");
        float tw = g.medir(t, 14);
        g.renglon(t, x + (W - tw) / 2, top + 24, 14, Color(TXT_DIM()));
        return;
    }
    scroll_res_chat.max = std::max(0.0, (double)res_chat.size() * FILA - H);
    scroll_res_chat.limitar();
    // Cerca del fondo: pedir la siguiente pagina.
    if (!res_chat_completa && !res_chat_buscando && scroll_res_chat.pos > scroll_res_chat.max - H) buscar_mas_chat();
    g.recortar(x, top, W, H);
    float y = (float)(top - scroll_res_chat.pos);
    const Chat* c = chat_de(chat_actual);
    for (size_t i = 0; i < res_chat.size(); i++, y += FILA) {
        if (y + FILA < top) continue;
        if (y > bottom) break;
        const Mensaje& m = res_chat[i];
        bool encima = mouse_x >= x && mouse_x < x + W - 24 && mouse_y >= y && mouse_y < y + FILA && mouse_y >= top && mouse_y < bottom;
        if ((int)i == elegido_chat) g.rect(x, y, W, FILA, Color(BG_SEL()));
        else if (encima) g.rect(x, y, W, FILA, Color(BG_HOVER()));
        std::wstring quien = m.propio ? L"You" : ((c && c->es_grupo) ? nombre_de(m.remitente) : (c ? c->nombre : nombre_de(m.chat)));
        std::wstring fecha = formatear_dia(m.ts) + L" " + formatear_hora(m.ts);
        float fw = g.medir(fecha, 12);
        g.renglon(fecha, x + W - 24 - fw, y + FILA * 0.2f, 12, Color(TXT_DIM()));
        g.renglon(quien, x + 16, y + FILA * 0.17f, letra_lista, Color(m.propio ? ACCENT() : TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - 56 - fw);
        std::wstring prev = m.texto.empty() ? nombre_tipo(m.tipo) : una_linea(m.texto);
        g.renglon(prev, x + 16, y + FILA * 0.53f, letra_lista - 2, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - 40);
        g.linea(x + 16, y + FILA - 0.5f, x + W - 16, y + FILA - 0.5f, Color(BORDE()));
    }
    if (res_chat_buscando) {
        float tw = g.medir(L"Searching...", 13);
        g.renglon(L"Searching...", x + (W - tw) / 2, y + 12, 13, Color(TXT_DIM()));
    }
    g.destapar();
    barra_scroll(scroll_res_chat, x + W - 10, top, H, res_chat.size() * FILA,
                 mouse_x > x + W - 24 && mouse_x < x + W && mouse_y > top && mouse_y < bottom, arrastrando_res_chat);
}

bool App::panel_resultados_chat() const { return busca_chat_abierta && elegido_chat < 0 && !chat_actual.empty(); }

// Clicks de la cabecera (lupa/cruz/campo/flechas) y del panel de resultados.
bool App::click_busqueda_chat(float x, float y, bool shift) {
    if (chat_actual.empty() || x < ancho_lista || info_abierto || tab_estados) return false;
    float xc = x_conv(), W = w_conv(), top = alto_cabecera(), bottom = g.alto - alto_pie;
    if (y < top) {
        // "Cargar todo" (a la derecha de la lupa) y la lupa (o la cruz cuando esta abierta).
        if (!busca_chat_abierta && x > xc + W - 56 && !seleccionando) {
            if (ajustes::actual().mensajes_por_chat > 0 && hay_mas_viejos && !cargando_todo) cargar_todo_el_chat();
            return true;
        }
        if (busca_chat_abierta ? (x > xc + W - 56) : (x > xc + W - 92 && x <= xc + W - 56)) {
            if (seleccionando) return false;
            if (busca_chat_abierta) cerrar_busqueda_chat();
            else abrir_busqueda_chat();
            return true;
        }
        if (!busca_chat_abierta) return false;
        float tx = xc + 16 + 40 + 14, cw = xc + W - 56 - tx, cy = (CABECERA_H - ALTO_CAMPO) / 2;
        bool nav = elegido_chat >= 0 && !res_chat.empty();
        if (nav && x >= tx + cw - NAVEGADOR_W + 106 && x < tx + cw) {
            if (x < tx + cw - NAVEGADOR_W + 132) mover_coincidencia(+1);
            else mover_coincidencia(-1);
            return true;
        }
        if (x >= tx && x < tx + cw && y >= cy && y < cy + ALTO_CAMPO) {
            buscador_chat.foco = true;
            campo.foco = false;
            buscador_chat.click(g, x, y, shift);
            return true;
        }
        // El resto de la cabecera con la busqueda abierta no abre la ficha.
        return true;
    }
    if (!panel_resultados_chat() || y >= bottom) return false;
    float H = bottom - top, FILA = fila_h();
    if (scroll_res_chat.max > 0 && x > xc + W - 24) {
        arrastrando_res_chat = true;
        agarrar_barra(scroll_res_chat, y, top, H, res_chat.size() * FILA);
        arrastrar_barra(scroll_res_chat, y, top, H, res_chat.size() * FILA);
        return true;
    }
    int i = (int)((y - top + scroll_res_chat.pos) / FILA);
    if (i >= 0 && i < (int)res_chat.size()) ir_a_coincidencia(i);
    return true;
}

bool App::clickeable_busqueda_chat(float x, float y) const {
    if (chat_actual.empty() || x < ancho_lista || info_abierto || tab_estados) return false;
    float xc = x_conv(), W = w_conv(), top = alto_cabecera(), bottom = g.alto - alto_pie;
    if (y < top) {
        if (busca_chat_abierta) {
            if (x > xc + W - 56) return true;
        } else {
            if (x > xc + W - 56) return ajustes::actual().mensajes_por_chat > 0 && hay_mas_viejos && !cargando_todo;
            if (x > xc + W - 92) return true;
            return false;
        }
        float tx = xc + 16 + 40 + 14, cw = xc + W - 56 - tx;
        return elegido_chat >= 0 && !res_chat.empty() && x >= tx + cw - NAVEGADOR_W + 106 && x < tx + cw;
    }
    if (!panel_resultados_chat() || y >= bottom) return false;
    if (x > xc + W - 24) return scroll_res_chat.max > 0;
    int i = (int)((y - top + scroll_res_chat.pos) / fila_h());
    return i >= 0 && i < (int)res_chat.size();
}
