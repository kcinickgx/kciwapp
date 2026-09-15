// Llamadas desde el cliente: los botones de la cabecera, la barra de la
// llamada en curso (nombre, tiempo, Mute, End) y, en las de video, la
// ventana de la llamada de WhatsApp Web puesta sobre la conversacion.
// El audio/video lo hace WhatsApp Web escondido (webwa).
#include "app.h"
#include "red.h"
#include "tema.h"
#include "toast.h"
#include "ventana_llamada.h"
#include "webwa.h"


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
    abrir_ventana_llamada();
    webwa::llamar(telefono_de(chat_actual), video);
    pedir_dibujo();
}

void App::abrir_ventana_llamada() {
    const Chat* c = chat_de(llamada_chat);
    bool con_foto = (c && c->tiene_foto) || (contactos.count(llamada_chat) && contactos[llamada_chat].tiene_foto);
    vllamada::abrir((HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), llamada_con, llamada_chat,
                    con_foto ? L"/foto/" + ancho(llamada_chat) : L"", llamada_video);
    vllamada::estado(llamada_saliente ? L"Calling..." : L"Connecting...", 0);
}

void App::atender_llamada(const std::string& chat, bool video) {
    const Chat* c = chat_de(chat);
    llamada_chat = chat;
    llamada_con = c ? c->nombre : nombre_de(chat);
    llamada_video = video;
    llamada_saliente = false;
    llamada_desde = 0;
    llamada_activa = true;
    abrir_ventana_llamada();
    webwa::querer_popout(video);
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
        if (!vllamada::abierta()) abrir_ventana_llamada();
        if (llamada_desde) vllamada::estado(L"", llamada_desde);
    } else {
        terminar_llamada_ui();
    }
    pedir_dibujo();
}

// La atendieron: desde aca corre el reloj.
void App::llamada_conectada() {
    if (!llamada_activa) return;
    if (!llamada_desde) llamada_desde = GetTickCount64();
    vllamada::estado(L"", llamada_desde);
    pedir_dibujo();
}

void App::terminar_llamada_ui() {
    llamada_activa = false;
    llamada_desde = 0;
    llamada_video = false;
    vllamada::cerrar();
    pedir_dibujo();
}

void App::colgar_llamada() {
    webwa::colgar();
    terminar_llamada_ui();
}

void App::ubicar_video_llamada() {}

float App::alto_barra_llamada() const { return 0.0f; }

void App::dibujar_barra_llamada() {}

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
    const Chat* c = chat_de(chat_actual);
    if (y < top && c && !c->es_grupo && llamadas_disponibles() && !busca_chat_abierta && !seleccionando && !llamada_activa)
        return x >= xc + W - 148 && x < xc + W - 72;
    return false;
}
