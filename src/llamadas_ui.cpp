// Llamadas desde el cliente: los botones de la cabecera, la barra de la
// llamada en curso (nombre, tiempo, Mute, End) y, en las de video, la
// ventana de la llamada de WhatsApp Web puesta sobre la conversacion.
// El audio/video lo hace WhatsApp Web escondido (webwa).
#include "app.h"
#include "red.h"
#include "tema.h"
#include "toast.h"
#include "webwa.h"

namespace {
constexpr float BARRA_LLAMADA_H = 44.0f;
}

bool App::llamadas_disponibles() const { return webwa::logueado(); }

// Telefono del jid (lo de antes de la @).
static std::string telefono_de(const std::string& jid) {
    size_t p = jid.find('@');
    return p == std::string::npos ? jid : jid.substr(0, p);
}

void App::iniciar_llamada(bool video) {
    const Chat* c = chat_de(chat_actual);
    if (!c || c->es_grupo || !llamadas_disponibles()) return;
    llamada_chat = chat_actual;
    llamada_con = c->nombre;
    llamada_video = video;
    llamada_saliente = true;
    llamada_desde = 0;  // arranca cuando WhatsApp Web la tiene en curso
    llamada_activa = true;
    webwa::llamar(telefono_de(chat_actual), video);
    pedir_dibujo();
}

void App::atender_llamada(const std::string& chat, bool video) {
    const Chat* c = chat_de(chat);
    llamada_chat = chat;
    llamada_con = c ? c->nombre : nombre_de(chat);
    llamada_video = video;
    llamada_saliente = false;
    llamada_desde = 0;
    llamada_activa = true;
    webwa::atender();
    if (chat != chat_actual) abrir_chat(chat);
    pedir_dibujo();
}

// Aviso de webwa: la llamada empezo o termino de verdad.
void App::llamada_cambio(bool en_curso) {
    if (en_curso) {
        if (!llamada_activa) {
            // Atendida desde el celu o desde la pagina: igual mostramos la barra.
            llamada_activa = true;
            llamada_con = chat_de(chat_actual) ? chat_de(chat_actual)->nombre : L"";
            llamada_chat = chat_actual;
        }
        if (!llamada_desde) llamada_desde = GetTickCount64();
        if (llamada_video) ubicar_video_llamada();
    } else {
        terminar_llamada_ui();
    }
    pedir_dibujo();
}

void App::terminar_llamada_ui() {
    llamada_activa = false;
    llamada_desde = 0;
    llamada_video = false;
    webwa::mostrar_llamada(false);
    pedir_dibujo();
}

void App::colgar_llamada() {
    webwa::colgar();
    terminar_llamada_ui();
}

// La videollamada va en una ventana aparte (solo el panel de la llamada).
void App::ubicar_video_llamada() { webwa::mostrar_llamada(llamada_activa && llamada_video); }

float App::alto_barra_llamada() const { return llamada_activa ? BARRA_LLAMADA_H : 0.0f; }

void App::dibujar_barra_llamada() {
    if (!llamada_activa) return;
    float x = x_conv(), W = w_conv(), y = alto_cabecera(), H = BARRA_LLAMADA_H;
    g.rect(x, y, W, H, Color(BG_PANEL()));
    g.linea(x, y + H - 0.5f, x + W, y + H - 0.5f, Color(BORDE()));
    g.renglon_fuente(L"Segoe MDL2 Assets", llamada_video ? L"" : L"", x + 16, y + 13, 16, Color(ACCENT()));
    std::wstring t = (llamada_video ? L"Video call" : L"Voice call") + std::wstring(L" · ") + llamada_con;
    if (llamada_desde) {
        unsigned long long s = (GetTickCount64() - llamada_desde) / 1000;
        wchar_t buf[32];
        swprintf(buf, 32, L" · %llu:%02llu", s / 60, s % 60);
        t += buf;
    } else {
        t += llamada_saliente ? L" · Calling..." : L" · Connecting...";
    }
    g.renglon(t, x + 44, y + 12, 14, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - 260);
    // Botones: Mute y End (rojo).
    float bw = 84, bh = 28, by = y + (H - bh) / 2;
    float mx = x + W - 2 * bw - 28, ex = x + W - bw - 14;
    bool mudo = webwa::silenciado();
    g.rect_redondo(mx, by, bw, bh, 6, Color(mudo ? ACCENT() : BG_CAMPO()));
    std::wstring mt = mudo ? L"Unmute" : L"Mute";
    float tw = g.medir(mt, 13);
    g.renglon(mt, mx + (bw - tw) / 2, by + 6, 13, Color(mudo ? 0xffffff : TXT()));
    g.rect_redondo(ex, by, bw, bh, 6, Color(0xf15c6d));
    tw = g.medir(L"End", 13);
    g.renglon(L"End", ex + (bw - tw) / 2, by + 6, 13, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

// Los botones de llamar en la cabecera (a la izquierda de la lupa).
void App::dibujar_botones_llamada() {
    const Chat* c = chat_de(chat_actual);
    if (!c || c->es_grupo || !llamadas_disponibles() || busca_chat_abierta || seleccionando) return;
    float x = x_conv(), W = w_conv();
    Color col(llamada_activa ? BORDE() : TXT_DIM());
    g.renglon_fuente(L"Segoe MDL2 Assets", L"", x + W - 100, 21, 18, col);
    g.renglon_fuente(L"Segoe MDL2 Assets", L"", x + W - 140, 21, 17, col);
}

bool App::click_llamada(float x, float y) {
    if (x < ancho_lista || info_abierto || visor) return false;
    float xc = x_conv(), W = w_conv(), top = alto_cabecera();
    // Barra de la llamada en curso.
    if (llamada_activa && y >= top && y < top + BARRA_LLAMADA_H) {
        float bw = 84, bh = 28, by = top + (BARRA_LLAMADA_H - bh) / 2;
        float mx = xc + W - 2 * bw - 28, ex = xc + W - bw - 14;
        if (y >= by && y < by + bh) {
            if (x >= ex && x < ex + bw) colgar_llamada();
            else if (x >= mx && x < mx + bw) webwa::silenciar(!webwa::silenciado());
        }
        return true;
    }
    // Botones de la cabecera.
    const Chat* c = chat_de(chat_actual);
    if (y < top && c && !c->es_grupo && llamadas_disponibles() && !busca_chat_abierta && !seleccionando && !llamada_activa) {
        if (x >= xc + W - 108 && x < xc + W - 72) {
            iniciar_llamada(true);
            return true;
        }
        if (x >= xc + W - 148 && x < xc + W - 108) {
            iniciar_llamada(false);
            return true;
        }
    }
    return false;
}

bool App::clickeable_llamada(float x, float y) const {
    if (x < ancho_lista || info_abierto || visor) return false;
    float xc = x_conv(), W = w_conv(), top = alto_cabecera();
    if (llamada_activa && y >= top && y < top + BARRA_LLAMADA_H) {
        float bw = 84, bh = 28, by = top + (BARRA_LLAMADA_H - bh) / 2;
        float mx = xc + W - 2 * bw - 28;
        return y >= by && y < by + bh && x >= mx && x < xc + W - 14;
    }
    const Chat* c = chat_de(chat_actual);
    if (y < top && c && !c->es_grupo && llamadas_disponibles() && !busca_chat_abierta && !seleccionando && !llamada_activa)
        return x >= xc + W - 148 && x < xc + W - 72;
    return false;
}
