// La parte visible de la actualizacion: el modal del chequeo (Cancel/Update,
// u OK si esta al dia), la pantalla de progreso mientras baja, y el relanzado
// al terminar.
#include "actualizar.h"
#include "app.h"

#include <shellapi.h>

#include "version.h"
#include "core.h"
#include "cuentas.h"
#include "red.h"
#include "tema.h"

namespace {

std::wstring megas(long long bytes) {
    wchar_t b[32];
    if (bytes >= 100 * 1024 * 1024) swprintf(b, 32, L"%.0f MB", bytes / 1048576.0);
    else if (bytes >= 1024 * 1024) swprintf(b, 32, L"%.1f MB", bytes / 1048576.0);
    else swprintf(b, 32, L"%.0f KB", bytes / 1024.0);
    return b;
}

}  // namespace

// Geometria del modal: la tarjeta centrada y sus botones.
struct GeoModal {
    float x, y, w, h;
    float b1x, b2x, by, bw, bh;  // boton izquierdo (Cancel) y derecho (Update/OK)
};

static GeoModal geo_modal(float W, float H, bool about = false) {
    GeoModal q;
    q.w = std::min(about ? 460.0f : 420.0f, W - 40);
    q.h = about ? 300 : 160;
    q.x = (W - q.w) / 2;
    q.y = (H - q.h) / 2;
    q.bw = 110;
    q.bh = 36;
    q.by = q.y + q.h - 20 - q.bh;
    q.b2x = q.x + q.w - 20 - q.bw;
    q.b1x = q.b2x - 12 - q.bw;
    return q;
}

// Chequea contra el manifiesto. A pedido abre el modal enseguida ("Checking")
// y muestra el resultado; automatico, solo si hay algo que bajar.
void App::verificar_actualizacion(bool a_pedido) {
    if (modal_actualizacion || actualizando) return;
    if (a_pedido) {
        modal_actualizacion = 1;
        texto_modal_actualizacion = L"Checking for updates...";
        campo.foco = false;
        pedir_dibujo();
    }
    actualizar::verificar(carpeta_exe(), [this, a_pedido] {
        actualizar::Estado e = actualizar::estado();
        // Cancelado mientras buscaba (o se abrio otro modal): nada.
        if (a_pedido ? modal_actualizacion != 1 : modal_actualizacion != 0) return;
        if (e.hay) {
            modal_actualizacion = 2;
            texto_modal_actualizacion = L"A new version is available (" + megas(e.total_bytes) + L", " + std::to_wstring(e.pendientes.size()) + (e.pendientes.size() == 1 ? L" file" : L" files") + L").";
            campo.foco = false;
        } else if (a_pedido) {
            modal_actualizacion = 3;
            texto_modal_actualizacion = e.error.empty() ? L"kciwapp is up to date." : e.error;
        }
        pedir_dibujo();
    });
}

static void cerrar_modal_actualizacion(App& a) {
    a.modal_actualizacion = 0;
    if (!a.config_pendiente && !a.selector_pendiente && !a.sin_sesion) a.campo.foco = true;
    a.pedir_dibujo();
}

static const float g_ancho_link_about = 210.0f;

void App::abrir_about() {
    if (modal_actualizacion || actualizando) return;
    modal_actualizacion = 4;
    campo.foco = false;
    pedir_dibujo();
}

// Los links del About: (y relativo al modal, texto, url).
static const struct {
    float y;
    const wchar_t* texto;
    const wchar_t* url;
} LINKS_ABOUT[] = {
    {150, L"github.com/kcinickgx/kciwapp", L"https://github.com/kcinickgx/kciwapp"},
    {172, L"Changelog", L"https://github.com/kcinickgx/kciwapp/blob/master/CHANGELOG.md"},
    {194, L"Third-party licenses", L"https://github.com/kcinickgx/kciwapp/blob/master/THIRD-PARTY.md"},
};

void App::dibujar_modal_actualizacion() {
    if (!modal_actualizacion) return;
    float W = g.ancho, H = g.alto;
    bool about = modal_actualizacion == 4;
    GeoModal q = geo_modal(W, H, about);
    // Todo lo de abajo apagado.
    g.rect(0, 0, W, H, Color(0x000000, 0.55f));
    g.rect_redondo(q.x, q.y, q.w, q.h, 12, Color(BG_PANEL()));
    g.borde_redondo(q.x, q.y, q.w, q.h, 12, Color(BORDE(), 1.0f));
    if (about) {
        g.renglon(L"kciwapp", q.x + 20, q.y + 18, 22, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(std::wstring(L"Version ") + KCIWAPP_VERSION + L"  \u00b7  build " + KCIWAPP_FECHA + L" (" + KCIWAPP_COMMIT + L")",
                  q.x + 20, q.y + 52, 13, Color(TXT_DIM()));
        g.renglon(L"A native WhatsApp client for Windows. Win32 + Direct2D, no frameworks;", q.x + 20, q.y + 80, 13, Color(TXT()));
        g.renglon(L"the WhatsApp side runs on whatsmeow. Unofficial, not affiliated with Meta.", q.x + 20, q.y + 100, 13, Color(TXT()));
        g.renglon(L"GPL-2.0-or-later \u00b7 \u00a9 2026 kcinick", q.x + 20, q.y + 124, 13, Color(TXT_DIM()));
        for (auto& l : LINKS_ABOUT) {
            float y = q.y + l.y;
            float tw = g.renglon(l.texto, q.x + 20, y, 13, Color(ACCENT()));
            g.linea(q.x + 20, y + 17, q.x + 20 + tw, y + 17, Color(ACCENT()));
        }
    } else {
        g.renglon(L"Check for updates", q.x + 20, q.y + 18, 16, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(texto_modal_actualizacion, q.x + 20, q.y + 52, 13.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, q.w - 40);
    }
    auto boton = [&](float x, const wchar_t* t, bool lleno) {
        if (lleno) g.rect_redondo(x, q.by, q.bw, q.bh, 8, Color(ACCENT()));
        else g.borde_redondo(x, q.by, q.bw, q.bh, 8, Color(BORDE()), 1.0f);
        float tw = g.medir(t, 14, lleno ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        g.renglon(t, x + (q.bw - tw) / 2, q.by + 9, 14, Color(lleno ? 0xffffff : TXT()), lleno ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    };
    if (modal_actualizacion == 1) boton(q.b2x, L"Cancel", false);
    else if (modal_actualizacion == 2) {
        boton(q.b1x, L"Cancel", false);
        boton(q.b2x, L"Update", true);
    } else boton(q.b2x, L"OK", true);
}

bool App::sobre_modal_actualizacion(float x, float y) const {
    if (!modal_actualizacion) return false;
    GeoModal q = geo_modal(g.ancho, g.alto, modal_actualizacion == 4);
    if (modal_actualizacion == 4)
        for (auto& l : LINKS_ABOUT)
            if (x >= q.x + 20 && x < q.x + 20 + g_ancho_link_about && y >= q.y + l.y && y < q.y + l.y + 18) return true;
    if (y < q.by || y >= q.by + q.bh) return false;
    if (x >= q.b2x && x < q.b2x + q.bw) return true;
    return modal_actualizacion == 2 && x >= q.b1x && x < q.b1x + q.bw;
}

bool App::click_modal_actualizacion(float x, float y) {
    if (!modal_actualizacion) return false;
    GeoModal q = geo_modal(g.ancho, g.alto, modal_actualizacion == 4);
    if (modal_actualizacion == 4) {
        for (auto& l : LINKS_ABOUT)
            if (x >= q.x + 20 && x < q.x + 20 + g_ancho_link_about && y >= q.y + l.y && y < q.y + l.y + 18) {
                ShellExecuteW(nullptr, L"open", l.url, nullptr, nullptr, SW_SHOWNORMAL);
                return true;
            }
    }
    bool en_fila = y >= q.by && y < q.by + q.bh;
    bool derecho = en_fila && x >= q.b2x && x < q.b2x + q.bw;
    bool izquierdo = en_fila && x >= q.b1x && x < q.b1x + q.bw;
    if (modal_actualizacion == 2 && derecho) {
        modal_actualizacion = 0;
        empezar_actualizacion();
    } else if (derecho || (modal_actualizacion == 2 && izquierdo)) {
        // Cancel mientras busca: el chequeo sigue en fondo pero ya no muestra nada.
        cerrar_modal_actualizacion(*this);
    }
    return true;  // el modal se come todo lo demas
}

bool App::tecla_modal_actualizacion(WPARAM vk) {
    if (!modal_actualizacion) return false;
    if (vk == VK_ESCAPE) cerrar_modal_actualizacion(*this);
    else if (vk == VK_RETURN) {
        if (modal_actualizacion == 2) {
            modal_actualizacion = 0;
            empezar_actualizacion();
        } else if (modal_actualizacion == 3 || modal_actualizacion == 4) cerrar_modal_actualizacion(*this);
    }
    return true;
}

void App::empezar_actualizacion() {
    if (actualizando) return;
    actualizando = true;
    campo.foco = false;
    pedir_dibujo();
    actualizar::bajar(
        carpeta_exe(), [this] { pedir_dibujo(); },
        [this](bool ok) {
            if (!ok) {
                actualizando = false;
                modal_actualizacion = 3;
                texto_modal_actualizacion = actualizar::estado().error;
                pedir_dibujo();
                return;
            }
            // Con todo bajado: el core cerrado (su exe puede cambiar), se
            // renombra y se vuelve a abrir el cliente con la misma cuenta.
            core::cerrar();
            actualizar::aplicar(carpeta_exe());
            cambiar_cuenta(cuentas::activa());
        });
}

void App::dibujar_actualizacion() {
    actualizar::Estado e = actualizar::estado();
    float W = g.ancho, H = g.alto;
    g.rect(0, 0, W, H, Color(BG_APP()));
    g.rect(0, 0, W, 6, Color(ACCENT()));
    float pw = std::min(520.0f, W - 40), px = (W - pw) / 2, py = std::max(20.0f, (H - 150) / 2);
    g.renglon(L"Updating kciwapp", px, py, 26, Color(TXT()), DWRITE_FONT_WEIGHT_LIGHT);
    std::wstring sub = e.actual.empty() ? L"Downloading..." : e.actual;
    g.renglon(sub, px, py + 48, 13, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, pw);
    float bx = px, by = py + 80, bw = pw, bh = 10;
    g.rect_redondo(bx, by, bw, bh, 5, Color(BG_CAMPO()));
    float f = e.total_bytes > 0 ? std::clamp((float)e.bajados / (float)e.total_bytes, 0.0f, 1.0f) : 0.0f;
    if (f > 0) g.rect_redondo(bx, by, bw * f, bh, 5, Color(ACCENT()));
    std::wstring t = megas(e.bajados) + L" / " + megas(e.total_bytes) + L"   ·   " + std::to_wstring(e.hechos) + L" / " + std::to_wstring(e.pendientes.size()) + L" files";
    g.renglon(t, px, by + 20, 12.5f, Color(TXT_DIM()));
    g.renglon(L"kciwapp will restart when done.", px, by + 48, 12, Color(TXT_DIM()));
}
