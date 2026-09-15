// Reenviar como WhatsApp: modo seleccion con tildes en los mensajes, barra
// arriba con "N selected", y un modal con la lista de chats (buscador y
// multi-seleccion) para elegir a quien mandarlos.
#include "app.h"
#include "red.h"
#include "tema.h"

namespace {
constexpr float MODAL_W = 440.0f, MODAL_H = 580.0f;
constexpr float FILA_DEST = 56.0f;

struct Rect {
    float x, y, w, h;
    bool tiene(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
Rect rect_modal(const App& a) {
    float w = std::min(MODAL_W, a.g.ancho - 40), h = std::min(MODAL_H, a.g.alto - 40);
    return {(a.g.ancho - w) / 2, (a.g.alto - h) / 2, w, h};
}
Rect rect_lista(const Rect& m) { return {m.x, m.y + 112, m.w, m.h - 112 - 64}; }
}  // namespace

void App::empezar_seleccion(int i) {
    seleccionando = true;
    seleccionados.clear();
    if (i >= 0 && i < (int)mensajes.size()) seleccionados.insert(mensajes[i].id);
    reaccion_msg = -1;
    sel_msg = -1;
    pedir_dibujo();
}

void App::terminar_seleccion() {
    seleccionando = false;
    seleccionados.clear();
    modal_reenvio = false;
    destinos.clear();
    pedir_dibujo();
}

void App::alternar_seleccion(int i) {
    if (i < 0 || i >= (int)mensajes.size() || mensajes[i].borrado) return;
    const std::string& id = mensajes[i].id;
    if (!seleccionados.erase(id)) seleccionados.insert(id);
    pedir_dibujo();
}

// La barra que reemplaza al pie (el campo de texto) mientras se seleccionan.
void App::dibujar_seleccion_barra() {
    if (!seleccionando) return;
    float x = x_conv(), W = w_conv(), H = alto_pie, y0 = g.alto - H;
    g.rect(x, y0, W, H, Color(BG_PANEL()));
    float cy = y0 + H / 2;
    g.renglon(L"✕", x + 20, cy - 12, 20, Color(TXT_DIM()));
    std::wstring t = std::to_wstring(seleccionados.size()) + L" selected";
    g.renglon(t, x + 60, cy - 10, 16, Color(TXT()));
    bool hay = !seleccionados.empty();
    float bx = x + W - 130;
    g.rect_redondo(bx, cy - 16, 110, 32, 16, Color(hay ? ACCENT() : BG_CAMPO()));
    float tw = g.medir(L"Forward", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"Forward", bx + (110 - tw) / 2, cy - 9, 14, Color(hay ? 0x111b21 : TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

bool App::click_seleccion(float x, float y) {
    if (!seleccionando) return false;
    float xc = x_conv(), W = w_conv();
    if (y >= g.alto - alto_pie && x >= xc) {
        if (x < xc + 50) terminar_seleccion();
        else if (x >= xc + W - 130 && x < xc + W - 20 && !seleccionados.empty()) {
            modal_reenvio = true;
            destinos.clear();
            buscador_reenvio.poner(L"");
            buscador_reenvio.foco = true;
            campo.foco = false;
            scroll_reenvio = Desplazable();
        }
        pedir_dibujo();
        return true;
    }
    if (x >= xc && y > alto_cabecera() && y < g.alto - alto_pie) {
        float ym = 0;
        int i = mensaje_en(y, &ym);
        if (i < 0) {
            // Click en el renglon pero fuera de la burbuja: tambien alterna.
            if (inicio.size() == vistas.size() + 1) {
                double desde = y - alto_cabecera() - 12 + conv.pos;
                size_t k = std::upper_bound(inicio.begin(), inicio.end() - 1, desde) - inicio.begin();
                if (k > 0) k--;
                if (k < vistas.size() && vistas[k].alto > 0) i = (int)k;
            }
        }
        if (i >= 0) alternar_seleccion(i);
        return true;
    }
    return false;
}

// ---- el modal de destinatarios --------------------------------------------

// Los chats que matchean el buscador.
static std::vector<int> candidatos_de(const App& a) {
    std::vector<int> r;
    std::wstring q = plano(a.buscador_reenvio.texto);
    while (!q.empty() && q.back() == L' ') q.pop_back();
    for (size_t i = 0; i < a.chats.size(); i++)
        if (q.empty() || plano(a.chats[i].nombre).find(q) != std::wstring::npos) r.push_back((int)i);
    return r;
}

void App::dibujar_modal_reenvio() {
    if (!modal_reenvio) return;
    if (buscador_reenvio.indicio.empty()) {
        buscador_reenvio.indicio = L"Search";
        buscador_reenvio.tamano = 14;
        buscador_reenvio.alto_max = 36;
    }
    g.rect(0, 0, g.ancho, g.alto, Color(0x000000, 0.55f));
    Rect m = rect_modal(*this);
    g.rect_redondo(m.x, m.y, m.w, m.h, 12, Color(BG_PANEL()));
    g.borde_redondo(m.x, m.y, m.w, m.h, 12, Color(BORDE()), 1.0f);
    g.renglon(L"Forward to", m.x + 20, m.y + 18, 18, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"✕", m.x + m.w - 40, m.y + 16, 20, Color(TXT_DIM()));
    // Buscador
    g.rect_redondo(m.x + 16, m.y + 60, m.w - 32, 36, 8, Color(BG_CAMPO()));
    buscador_reenvio.dibujar(g, m.x + 16, m.y + 60, m.w - 32, 36, ahora);
    // Lista
    Rect l = rect_lista(m);
    std::vector<int> cand = candidatos_de(*this);
    scroll_reenvio.max = std::max(0.0, (double)cand.size() * FILA_DEST - l.h);
    scroll_reenvio.limitar();
    if (!scroll_reenvio.quieto()) pedir_dibujo();
    g.recortar(l.x, l.y, l.w, l.h);
    float y = l.y - (float)scroll_reenvio.pos;
    for (int idx : cand) {
        if (y + FILA_DEST >= l.y && y <= l.y + l.h) {
            const Chat& c = chats[idx];
            bool elegido = destinos.count(c.jid) > 0;
            if (mouse_x >= l.x && mouse_x < l.x + l.w && mouse_y >= std::max(y, l.y) && mouse_y < std::min(y + FILA_DEST, l.y + l.h))
                g.rect(l.x, y, l.w, FILA_DEST, Color(BG_HOVER()));
            float r = 20, cx = l.x + 20 + r, cy = y + FILA_DEST / 2;
            Imagen* foto = nullptr;
            if (c.tiene_foto || (!c.es_grupo && contactos.count(c.jid) && contactos[c.jid].tiene_foto))
                foto = &imagen("foto:" + c.jid, L"/foto/" + ancho(c.jid), false);
            if (foto && foto->bmp) g.bitmap_circular(foto->bmp.Get(), cx, cy, r);
            else {
                g.circulo(cx, cy, r, Color(0x6b7c85));
                std::wstring inicial = c.nombre.empty() ? L"?" : c.nombre.substr(0, 1);
                float iw = g.medir(inicial, 17);
                g.renglon(inicial, cx - iw / 2, cy - 11, 17, Color(0xdfe5e7));
            }
            g.renglon(c.nombre, cx + r + 14, cy - 10, 15, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, l.w - 130);
            // Tilde a la derecha.
            float tx = l.x + l.w - 44, ty = cy - 11;
            g.rect_redondo(tx, ty, 22, 22, 11, Color(elegido ? ACCENT() : BG_CAMPO()));
            if (!elegido) g.borde_redondo(tx, ty, 22, 22, 11, Color(BORDE()), 1.0f);
            if (elegido) tildes(tx + 3, ty + 4, false, Color(0x111b21));
        }
        y += FILA_DEST;
        if (y > l.y + l.h) break;
    }
    if (cand.empty()) g.renglon(L"No chats found", l.x + 20, l.y + 16, 14, Color(TXT_DIM()));
    g.destapar();
    // Pie: cuantos mensajes y a cuantos, y el boton.
    float by = m.y + m.h - 52;
    g.linea(m.x, by - 12, m.x + m.w, by - 12, Color(BORDE()));
    std::wstring info = std::to_wstring(seleccionados.size()) + (seleccionados.size() == 1 ? L" message" : L" messages");
    if (!destinos.empty()) info += L" to " + std::to_wstring(destinos.size()) + (destinos.size() == 1 ? L" chat" : L" chats");
    g.renglon(info, m.x + 20, by + 10, 13, Color(TXT_DIM()));
    bool listo = !destinos.empty();
    g.rect_redondo(m.x + m.w - 116, by, 100, 36, 18, Color(listo ? ACCENT() : BG_CAMPO()));
    float tw = g.medir(L"Send", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"Send", m.x + m.w - 116 + (100 - tw) / 2, by + 9, 14, Color(listo ? 0x111b21 : TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

bool App::click_modal_reenvio(float x, float y) {
    if (!modal_reenvio) return false;
    Rect m = rect_modal(*this);
    if (!m.tiene(x, y)) {
        modal_reenvio = false;
        pedir_dibujo();
        return true;
    }
    if (y < m.y + 50 && x > m.x + m.w - 50) {
        modal_reenvio = false;
        pedir_dibujo();
        return true;
    }
    if (y >= m.y + 60 && y < m.y + 96) {
        buscador_reenvio.foco = true;
        buscador_reenvio.click(g, x, y, false);
        pedir_dibujo();
        return true;
    }
    Rect l = rect_lista(m);
    if (l.tiene(x, y)) {
        std::vector<int> cand = candidatos_de(*this);
        int k = (int)((y - l.y + (float)scroll_reenvio.pos) / FILA_DEST);
        if (k >= 0 && k < (int)cand.size()) {
            const std::string& jid = chats[cand[k]].jid;
            if (!destinos.erase(jid)) destinos.insert(jid);
        }
        pedir_dibujo();
        return true;
    }
    float by = m.y + m.h - 52;
    if (x >= m.x + m.w - 116 && x < m.x + m.w - 16 && y >= by && y < by + 36 && !destinos.empty()) {
        enviar_reenvio();
        return true;
    }
    return true;
}

bool App::rueda_modal_reenvio(float x, float y, float delta) {
    if (!modal_reenvio) return false;
    scroll_reenvio.rodar(-delta / 120.0f * 3 * 40.0f);
    pedir_dibujo();
    return true;
}

// Manda los mensajes marcados (en orden) a cada destino, uno tras otro.
void App::enviar_reenvio() {
    std::vector<std::string> ids;
    for (auto& m : mensajes)
        if (seleccionados.count(m.id)) ids.push_back(m.id);
    std::vector<std::string> dest(destinos.begin(), destinos.end());
    std::string chat = chat_actual;
    terminar_seleccion();
    aviso_estado = L"Forwarding...";
    pedir_dibujo();
    red::en_fondo([this, chat, ids, dest] {
        int fallos = 0;
        for (auto& d : dest)
            for (auto& id : ids) {
                Respuesta r = red::mandar_json(L"/reenviar", "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"destino\":" + json_texto(d) + "}");
                if (!r.ok()) fallos++;
                else {
                    Json j = Json::parsear(r.cuerpo);
                    red::en_ui([this, j] { agregar_mensaje(Mensaje::de_json(j)); });
                }
            }
        red::en_ui([this, fallos] {
            aviso_estado = fallos ? L"Some messages could not be forwarded" : L"";
            pedir_dibujo();
        });
    });
}
