// La tab "Status": los estados (status@broadcast) fuera de la lista de chats,
// agrupados por quien los publico (My status arriba, Recent y Viewed), y un
// visor en la columna derecha con barra de segmentos, avance automatico y
// campo de respuesta. El visto se manda solo por el estado que se muestra.
#include "app.h"

#include <algorithm>

#include "cache.h"
#include "red.h"
#include "tema.h"

namespace {
constexpr char STATUS[] = "status@broadcast";
constexpr float FILA_ESTADO = 72.0f;
constexpr float TITULO_SECCION = 34.0f;
constexpr long long UN_DIA = 24LL * 3600 * 1000;
constexpr unsigned long long DURA_TEXTO = 5000, DURA_FOTO = 5000;
}  // namespace

bool App::respondiendo_estado() const { return tab_estados && !estado_de.empty() && estado_de != mi_jid; }

// Entrar o salir de la tab. Al entrar se abre (escondido) el chat de estados
// para tener los mensajes por el camino de siempre; al salir vuelve el chat
// que estaba.
void App::abrir_tab_estados(bool si) {
    if (si == tab_estados) return;
    if (si) {
        chat_antes_estados = chat_actual == STATUS ? "" : chat_actual;
        tab_estados = true;
        estado_de.clear();
        estado_idx = -1;
        abrir_chat(STATUS);
        if (estados_vistos.empty()) {
            // Lo visto se recuerda en la cache (ids separados por coma).
            std::string v = cache::valor("estados_vistos");
            size_t a = 0;
            while (a < v.size()) {
                size_t b = v.find(',', a);
                if (b == std::string::npos) b = v.size();
                if (b > a) estados_vistos.insert(v.substr(a, b - a));
                a = b + 1;
            }
        }
    } else {
        tab_estados = false;
        estado_de.clear();
        estado_idx = -1;
        if (reproduciendo_id.size() && chat_actual == STATUS) reproductor.parar();
        if (!chat_antes_estados.empty()) abrir_chat(chat_antes_estados);
        else cerrar_chat();
    }
    lista.ir(0, true);
    pedir_dibujo();
}

// Cierra el chat abierto: queda la pantalla vacia.
void App::cerrar_chat() {
    if (chat_actual.empty()) return;
    borradores[chat_actual] = campo.texto;
    borradores_adjunto[chat_actual] = adjunto;
    recordar_chat();
    chat_actual.clear();
    mensajes.clear();
    vistas.clear();
    inicio.clear();
    layout_pendiente = 0;
    layout_gen++;
    tandas_listas.clear();
    cargando_mensajes = false;
    conv = Desplazable();
    respondiendo.reset();
    editando.reset();
    sel_msg = -1;
    campo.poner(L"");
    adjunto.reset();
    campo.indicio = L"Type a message";
}

void App::guardar_estados_vistos() {
    std::string v;
    for (auto& id : estados_vistos) v += (v.empty() ? "" : ",") + id;
    cache::guardar_valor("estados_vistos", v);
}

// Agrupa los estados de las ultimas 24 h por remitente: mi fila primero,
// despues los que tienen algo sin ver (mas nuevo arriba), despues los vistos.
void App::armar_grupos_estado() {
    grupos_estado.clear();
    if (chat_actual != STATUS) return;
    long long limite = ahora_ms() - UN_DIA;
    std::map<std::string, size_t> pos;
    for (size_t i = 0; i < mensajes.size(); i++) {
        const Mensaje& m = mensajes[i];
        if (m.borrado || m.ts < limite) continue;
        std::string quien = m.propio ? mi_jid : m.remitente;
        auto it = pos.find(quien);
        if (it == pos.end()) {
            pos[quien] = grupos_estado.size();
            grupos_estado.push_back({quien, {}, 0, false, m.propio});
            it = pos.find(quien);
        }
        GrupoEstado& gr = grupos_estado[it->second];
        gr.idx.push_back((int)i);
        gr.ultimo_ts = std::max(gr.ultimo_ts, m.ts);
        if (!m.propio && !estados_vistos.count(m.id)) gr.sin_ver = true;
    }
    std::stable_sort(grupos_estado.begin(), grupos_estado.end(), [](const GrupoEstado& a, const GrupoEstado& b) {
        if (a.propio != b.propio) return a.propio;
        if (a.sin_ver != b.sin_ver) return a.sin_ver;
        return a.ultimo_ts > b.ultimo_ts;
    });
    // Se limpia lo visto de estados que ya no estan (mas de un dia).
    if (estados_vistos.size() > 2000) {
        std::set<std::string> vivos;
        for (auto& gr : grupos_estado)
            for (int i : gr.idx) vivos.insert(mensajes[i].id);
        for (auto it = estados_vistos.begin(); it != estados_vistos.end();)
            it = vivos.count(*it) ? std::next(it) : estados_vistos.erase(it);
        guardar_estados_vistos();
    }
}

bool App::hay_estados_nuevos() {
    if (chat_actual != STATUS) {
        // Sin el chat abierto: lo dice el contador de la lista.
        const Chat* c = chat_de(STATUS);
        return c && c->no_leidos > 0;
    }
    armar_grupos_estado();
    for (auto& gr : grupos_estado)
        if (gr.sin_ver) return true;
    return false;
}

// El anillo de estados alrededor del avatar: un segmento por estado.
void App::dibujar_anillo_estado(float cx, float cy, float r, int segmentos, int vistos, bool propio) {
    Color con = Color(ACCENT()), sin = Color(TXT_DIM(), 0.45f);
    if (segmentos <= 0) {
        g.borde_redondo(cx - r, cy - r, 2 * r, 2 * r, r, sin, 2.0f);
        return;
    }
    if (segmentos == 1) {
        g.borde_redondo(cx - r, cy - r, 2 * r, 2 * r, r, vistos >= 1 && !propio ? sin : con, 2.5f);
        return;
    }
    // Arcos con puntos (sin geometria de D2D): 48 puntos por vuelta, hueco entre segmentos.
    const int PUNTOS = 60;
    float paso = 6.2831853f / PUNTOS;
    for (int k = 0; k < PUNTOS; k++) {
        float frac = (float)k / PUNTOS;
        int seg = (int)(frac * segmentos);
        float dentro = frac * segmentos - seg;
        if (dentro > 0.9f) continue;  // el hueco
        float a = -1.5707963f + k * paso;
        bool visto = !propio && seg < vistos;
        g.circulo(cx + r * std::cos(a), cy + r * std::sin(a), 1.4f, visto ? sin : con);
    }
}

// La lista de la tab Status (columna izquierda, debajo de las tabs).
void App::dibujar_lista_estados(float top) {
    float W = ancho_lista, H = g.alto - top;
    armar_grupos_estado();
    // Alto total: mi fila + secciones.
    bool hay_recent = false, hay_viewed = false;
    for (auto& gr : grupos_estado)
        if (!gr.propio) (gr.sin_ver ? hay_recent : hay_viewed) = true;
    float total = FILA_ESTADO + 8;
    if (hay_recent) total += TITULO_SECCION;
    if (hay_viewed) total += TITULO_SECCION;
    for (auto& gr : grupos_estado)
        if (!gr.propio) total += FILA_ESTADO;
    lista.max = std::max(0.0, (double)total - H);
    lista.limitar();
    g.recortar(0, top, W, H);
    float y = (float)(top - lista.pos) + 4;
    auto fila = [&](const GrupoEstado* gr, bool elegida) {
        if (elegida) g.rect(0, y, W, FILA_ESTADO, Color(BG_SEL()));
        float r = 24, cx = 16 + r + 4, cy = y + FILA_ESTADO / 2;
        std::string jid = gr ? gr->jid : mi_jid;
        Imagen* foto = nullptr;
        bool tiene = contactos.count(jid) && contactos[jid].tiene_foto;
        if (const Chat* c = chat_de(jid)) tiene = tiene || c->tiene_foto;
        if (tiene || jid == mi_jid) foto = &imagen("foto:" + jid, L"/foto/" + ancho(jid), false);
        if (foto && foto->bmp) g.bitmap_circular(foto->bmp.Get(), cx, cy, r - 4);
        else {
            g.circulo(cx, cy, r - 4, Color(0x6b7c85));
            std::wstring n = gr ? nombre_de(jid) : L"Me";
            std::wstring inicial = n.empty() ? L"?" : n.substr(0, 1);
            float iw = g.medir(inicial, 16);
            g.renglon(inicial, cx - iw / 2, cy - 10, 16, Color(0xdfe5e7));
        }
        int vistos = 0;
        if (gr)
            for (int i : gr->idx) vistos += estados_vistos.count(mensajes[i].id) ? 1 : 0;
        dibujar_anillo_estado(cx, cy, r, gr ? (int)gr->idx.size() : 0, vistos, !gr || gr->propio);
        float tx = cx + r + 14;
        if (!gr) {
            g.renglon(L"My status", tx, y + 16, letra_lista, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - tx - 16);
            // Cuantos tengo publicados, o la invitacion.
            int mios = 0;
            for (auto& g2 : grupos_estado)
                if (g2.propio) mios = (int)g2.idx.size();
            std::wstring sub = mios ? std::to_wstring(mios) + (mios == 1 ? L" update" : L" updates") : L"Click to add status update";
            g.renglon(sub, tx, y + 40, letra_lista - 2, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - tx - 16);
            // El + para publicar.
            g.circulo(cx + r - 6, cy + r - 6, 9, Color(ACCENT()));
            g.renglon(L"+", cx + r - 10.5f, cy + r - 17, 15, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        } else {
            std::wstring hora = dia_de(gr->ultimo_ts) == dia_de(ahora_ms()) ? L"Today " + formatear_hora(gr->ultimo_ts) : L"Yesterday " + formatear_hora(gr->ultimo_ts);
            g.renglon(nombre_de(gr->jid), tx, y + 16, letra_lista, Color(gr->sin_ver ? TXT() : TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - tx - 16);
            g.renglon(hora, tx, y + 40, letra_lista - 2, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - tx - 16);
        }
        y += FILA_ESTADO;
    };
    fila(nullptr, estado_de == mi_jid && !mi_jid.empty());
    y += 8;
    if (hay_recent) {
        g.renglon(L"RECENT", 16, y + 10, 12, Color(ACCENT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += TITULO_SECCION;
        for (auto& gr : grupos_estado)
            if (!gr.propio && gr.sin_ver) fila(&gr, estado_de == gr.jid);
    }
    if (hay_viewed) {
        g.renglon(L"VIEWED", 16, y + 10, 12, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += TITULO_SECCION;
        for (auto& gr : grupos_estado)
            if (!gr.propio && !gr.sin_ver) fila(&gr, estado_de == gr.jid);
    }
    if (grupos_estado.empty() || (!hay_recent && !hay_viewed)) {
        std::wstring t = cargando_mensajes ? L"Loading..." : L"No recent updates";
        float tw = g.medir(t, 13);
        g.renglon(t, (W - tw) / 2, y + 30, 13, Color(TXT_DIM()));
    }
    g.destapar();
    barra_scroll(lista, W - 10, top, H, lista.max + H, mouse_x > W - 24 && mouse_x < W && mouse_y > top, arrastrando_lista);
}

// Que fila de la lista de estados esta en y: "" ninguna, mi_jid la mia, o el jid.
std::string App::fila_estado_en(float y) {
    float top = top_lista();
    if (y < top) return "";
    float yy = (float)(top - lista.pos) + 4;
    if (y >= yy && y < yy + FILA_ESTADO) return mi_jid;
    yy += FILA_ESTADO + 8;
    bool hay_recent = false, hay_viewed = false;
    for (auto& gr : grupos_estado)
        if (!gr.propio) (gr.sin_ver ? hay_recent : hay_viewed) = true;
    for (int pasada = 0; pasada < 2; pasada++) {
        bool recent = pasada == 0;
        if (recent ? !hay_recent : !hay_viewed) continue;
        yy += TITULO_SECCION;
        for (auto& gr : grupos_estado) {
            if (gr.propio || gr.sin_ver != recent) continue;
            if (y >= yy && y < yy + FILA_ESTADO) return gr.jid;
            yy += FILA_ESTADO;
        }
    }
    return "";
}

bool App::click_lista_estados(float x, float y) {
    if (!tab_estados || x >= ancho_lista) return false;
    std::string jid = fila_estado_en(y);
    if (jid.empty()) return y >= top_lista();
    if (jid == mi_jid) {
        // Mi fila: si no tengo estados, va derecho al compositor.
        bool tengo = false;
        for (auto& gr : grupos_estado) tengo = tengo || gr.propio;
        estado_de = mi_jid;
        estado_idx = tengo ? 0 : -1;
        estado_desde = GetTickCount64();
        campo.foco = true;
        pedir_dibujo();
        return true;
    }
    // El primero sin ver, o el primero.
    for (auto& gr : grupos_estado) {
        if (gr.jid != jid) continue;
        int k = 0;
        for (size_t n = 0; n < gr.idx.size(); n++)
            if (!estados_vistos.count(mensajes[gr.idx[n]].id)) {
                k = (int)n;
                break;
            }
        mostrar_estado(jid, k);
        break;
    }
    return true;
}

// Los indices (en mensajes) de los estados del remitente elegido.
const std::vector<int>* App::estados_de(const std::string& jid) {
    for (auto& gr : grupos_estado)
        if (gr.jid == jid) return &gr.idx;
    return nullptr;
}

// Muestra el estado k del remitente: lo marca visto (y manda el visto, si es
// ajeno), arranca el audio si es una nota, y pone el reloj del avance.
void App::mostrar_estado(const std::string& jid, int k) {
    armar_grupos_estado();
    const std::vector<int>* idx = estados_de(jid);
    if (!idx || k < 0 || k >= (int)idx->size()) return;
    estado_de = jid;
    estado_idx = k;
    estado_desde = GetTickCount64();
    const Mensaje& m = mensajes[(*idx)[k]];
    if (!m.propio && !estados_vistos.count(m.id)) {
        estados_vistos.insert(m.id);
        guardar_estados_vistos();
        std::string cuerpo = "{\"chat\":\"" + std::string(STATUS) + "\",\"ids\":[" + json_texto(m.id) + "]}";
        red::en_fondo([cuerpo] { red::mandar_json(L"/leido", cuerpo); });
        // El contador del chat de estados baja de a uno.
        for (auto& c : chats)
            if (c.jid == STATUS && c.no_leidos > 0) c.no_leidos--;
    }
    if (reproduciendo_id != m.id && !reproduciendo_id.empty()) reproductor.parar(), reproduciendo_id.clear();
    if ((m.tipo == "audio" || m.tipo == "nota") && m.media && reproductor_ok) reproducir_audio((*idx)[k]);
    campo.indicio = jid == mi_jid ? std::wstring(L"Share a status · ") + LETRA_ESTADO[estado_letra] : L"Reply to " + nombre_de(jid) + L"...";
    campo.foco = true;
    pedir_dibujo();
}

// dir = +1 el siguiente (y al terminar los de este, el siguiente contacto), -1 el anterior.
void App::avanzar_estado(int dir) {
    if (estado_de.empty()) return;
    const std::vector<int>* idx = estados_de(estado_de);
    if (!idx) return;
    int k = estado_idx + dir;
    if (k >= 0 && k < (int)idx->size()) {
        mostrar_estado(estado_de, k);
        return;
    }
    if (dir > 0) {
        // Al siguiente contacto de la lista (en el orden que se ve).
        bool despues = false;
        for (int pasada = 0; pasada < 2; pasada++)
            for (auto& gr : grupos_estado) {
                if (gr.propio || (gr.sin_ver != (pasada == 0))) continue;
                if (despues) {
                    mostrar_estado(gr.jid, 0);
                    return;
                }
                if (gr.jid == estado_de) despues = true;
            }
        // No hay mas: se cierra el visor.
        estado_de.clear();
        estado_idx = -1;
        if (!reproduciendo_id.empty()) reproductor.parar(), reproduciendo_id.clear();
    } else {
        mostrar_estado(estado_de, 0);
    }
    pedir_dibujo();
}

// Cuanto dura en pantalla el estado actual (ms); 0 = no avanza solo.
unsigned long long App::duracion_estado(const Mensaje& m) const {
    if (m.tipo == "audio" || m.tipo == "nota") return 0;  // avanza cuando termina de sonar
    if (m.tipo == "video" || m.tipo == "gif") return m.media && m.media->segundos > 0 ? (unsigned long long)m.media->segundos * 1000 + 500 : DURA_FOTO;
    if (m.media) return DURA_FOTO;
    return DURA_TEXTO + (unsigned long long)std::min<size_t>(m.texto.size(), 300) * 20;  // textos largos, mas tiempo
}

// Se llama en cada frame: avance automatico.
void App::tic_estados() {
    if (!tab_estados || estado_de.empty() || estado_idx < 0 || visor || menu_abierto) return;
    const std::vector<int>* idx = estados_de(estado_de);
    if (!idx || estado_idx >= (int)idx->size()) return;
    const Mensaje& m = mensajes[(*idx)[estado_idx]];
    if (estado_pausado) return;
    if (m.tipo == "audio" || m.tipo == "nota") {
        if (reproduciendo_id == m.id && reproductor.terminado()) avanzar_estado(+1);
        return;
    }
    if (GetTickCount64() - estado_desde >= duracion_estado(m)) avanzar_estado(+1);
}

// El visor, en la columna derecha (arriba del pie).
void App::dibujar_visor_estado() {
    float x = x_conv(), W = w_conv(), H = g.alto - alto_pie;
    g.rect(x, 0, W, g.alto, Color(BG_SEL()));
    if (estado_de.empty()) {
        std::wstring t = L"Click a contact to see their status updates";
        float tw = g.medir(t, 15);
        g.renglon(t, x + (W - tw) / 2, H / 2 - 10, 15, Color(TXT_DIM()));
        return;
    }
    const std::vector<int>* idx = estados_de(estado_de);
    if (!idx || estado_idx < 0 || estado_idx >= (int)idx->size()) {
        // Mi fila sin estados: solo el compositor abajo.
        std::wstring t = estado_de == mi_jid ? L"Write something below to share a status" : L"No updates";
        float tw = g.medir(t, 15);
        g.renglon(t, x + (W - tw) / 2, H / 2 - 10, 15, Color(TXT_DIM()));
        return;
    }
    const Mensaje& m = mensajes[(*idx)[estado_idx]];
    // Barra de segmentos.
    int n = (int)idx->size();
    float sx = x + 16, sw = (W - 32 - 4.0f * (n - 1)) / n, sy = 10;
    for (int k = 0; k < n; k++) {
        float bx = sx + k * (sw + 4);
        g.rect_redondo(bx, sy, sw, 3, 1.5f, Color(0xffffff, 0.35f));
        float f = k < estado_idx ? 1.0f : 0.0f;
        if (k == estado_idx) {
            unsigned long long d = duracion_estado(m);
            if (d == 0 && reproduciendo_id == m.id) {
                double dur = reproductor.duracion();
                f = dur > 0 ? (float)std::clamp(reproductor.posicion() / dur, 0.0, 1.0) : 0.0f;
            } else if (d > 0) f = std::clamp((float)(GetTickCount64() - estado_desde) / (float)d, 0.0f, 1.0f);
        }
        if (f > 0) g.rect_redondo(bx, sy, sw * f, 3, 1.5f, Color(0xffffff));
    }
    // Cabecera: foto, nombre, hora, cruz.
    float r = 18, cx = x + 16 + r, cy = 42;
    std::string jid = estado_de;
    Imagen* foto = nullptr;
    bool tiene = contactos.count(jid) && contactos[jid].tiene_foto;
    if (const Chat* c = chat_de(jid)) tiene = tiene || c->tiene_foto;
    if (tiene || jid == mi_jid) foto = &imagen("foto:" + jid, L"/foto/" + ancho(jid), false);
    if (foto && foto->bmp) g.bitmap_circular(foto->bmp.Get(), cx, cy, r);
    else g.circulo(cx, cy, r, Color(0x6b7c85));
    g.renglon(jid == mi_jid ? L"My status" : nombre_de(jid), cx + r + 12, 24, 15, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD, W - 160);
    g.renglon(formatear_dia(m.ts) + L" " + formatear_hora(m.ts), cx + r + 12, 44, 12, Color(TXT_DIM()));
    g.renglon(L"✕", x + W - 40, 22, 20, Color(TXT_DIM()));
    if (m.propio) g.renglon_fuente(L"Segoe MDL2 Assets", L"", x + W - 76, 26, 15, Color(0xf15c6d));  // borrar el mio
    // El contenido, centrado en lo que queda.
    float top = 72, alto = H - top - 16, cw = std::min(W - 80, 520.0f), ccx = x + W / 2;
    if (m.media && (con_imagen(m.tipo))) {
        float rel = (m.media->ancho > 0 && m.media->alto > 0) ? (float)m.media->alto / m.media->ancho : 1.0f;
        float mw = cw, mh = mw * rel;
        float maxh = alto - (m.texto.empty() ? 0 : 60);
        if (mh > maxh) {
            mh = maxh;
            mw = mh / rel;
        }
        float mx = ccx - mw / 2, my = top + (alto - mh - (m.texto.empty() ? 0 : 60)) / 2;
        dibujar_foto(m, mx, my, mw, mh, 10);
        if (m.tipo == "video" || m.tipo == "gif") {
            g.circulo(ccx, my + mh / 2, 28, Color(0x000000, 0.55f));
            g.renglon(L"▶", ccx - 9, my + mh / 2 - 13, 20, Color(0xffffff));
        }
        if (!m.texto.empty()) g.renglon(m.texto, mx, my + mh + 16, 15, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, mw);
        estado_media_rect = {mx, my, mw, mh};
    } else if (m.media && (m.tipo == "audio" || m.tipo == "nota")) {
        float aw = std::min(cw, 360.0f), ah = 58, ax = ccx - aw / 2, ay = top + (alto - ah) / 2;
        dibujar_audio((*idx)[estado_idx], ax, ay, aw, ah);
        estado_media_rect = {ax, ay, aw, ah};
    } else {
        // Texto: grande y centrado, sobre un fondo de color.
        estado_media_rect = {0, 0, 0, 0};
        g.rect_redondo(x + 24, top, W - 48, alto, 14, Color(0x1f2c34));
        float tam = m.texto.size() < 40 ? 30.0f : (m.texto.size() < 120 ? 24.0f : 19.0f);
        auto l = g.texto(m.texto, tam, cw, DWRITE_FONT_WEIGHT_NORMAL);
        if (l) {
            l->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            DWRITE_TEXT_METRICS tm;
            l->GetMetrics(&tm);
            g.recortar(x + 24, top, W - 48, alto);
            g.dibujar_texto(l.Get(), ccx - cw / 2, top + std::max(10.0f, (alto - tm.height) / 2), Color(0xffffff));
            g.destapar();
        }
    }
    // Flechas para pasar.
    if (estado_idx > 0 || true) g.renglon(L"‹", x + 14, H / 2 - 22, 36, Color(TXT_DIM()));
    g.renglon(L"›", x + W - 34, H / 2 - 22, 36, Color(TXT_DIM()));
}

bool App::click_visor_estado(float x, float y) {
    if (!tab_estados || x < ancho_lista) return false;
    float xc = x_conv(), W = w_conv(), H = g.alto - alto_pie;
    if (y >= H) return false;  // el pie (compositor / respuesta) lo maneja el de siempre
    if (estado_de.empty()) return true;
    if (x > xc + W - 56 && y < 60) {
        estado_de.clear();
        estado_idx = -1;
        if (!reproduciendo_id.empty()) reproductor.parar(), reproduciendo_id.clear();
        pedir_dibujo();
        return true;
    }
    const std::vector<int>* idx = estados_de(estado_de);
    if (!idx || estado_idx < 0 || estado_idx >= (int)idx->size()) return true;
    int i = (*idx)[estado_idx];
    const Mensaje& m = mensajes[i];
    if (m.propio && x > xc + W - 90 && x < xc + W - 56 && y < 60) {
        borrar(i);  // Delete for everyone (retira el estado)
        return true;
    }
    // Sobre la media: foto/video se abre, audio se maneja como en el chat.
    if (estado_media_rect.w > 0 && x >= estado_media_rect.x && x <= estado_media_rect.x + estado_media_rect.w &&
        y >= estado_media_rect.y && y <= estado_media_rect.y + estado_media_rect.h) {
        if (m.tipo == "audio" || m.tipo == "nota") click_audio(i, x - estado_media_rect.x, y - estado_media_rect.y, estado_media_rect.w, estado_media_rect.h);
        else abrir_media(i);
        return true;
    }
    if (x < xc + W / 3) avanzar_estado(-1);
    else avanzar_estado(+1);
    return true;
}

bool App::clickeable_visor_estado(float x, float y) const {
    if (!tab_estados || x < ancho_lista) return false;
    float xc = x_conv(), W = w_conv(), H = g.alto - alto_pie;
    if (y >= H || estado_de.empty()) return false;
    return true;  // todo el visor reacciona (pasar, cerrar, abrir la media)
}
