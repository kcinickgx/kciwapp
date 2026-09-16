// La parte visible de la actualizacion: el aviso debajo del buscador de la
// lista ("Update available" / "up to date"), la pantalla de progreso mientras
// baja, y el relanzado al terminar.
#include "actualizar.h"
#include "app.h"
#include "core.h"
#include "cuentas.h"
#include "red.h"
#include "tema.h"

namespace {

constexpr float BANNER_Y = 100.0f;
constexpr float BANNER_H = 30.0f;

std::wstring megas(long long bytes) {
    wchar_t b[32];
    if (bytes >= 100 * 1024 * 1024) swprintf(b, 32, L"%.0f MB", bytes / 1048576.0);
    else if (bytes >= 1024 * 1024) swprintf(b, 32, L"%.1f MB", bytes / 1048576.0);
    else swprintf(b, 32, L"%.0f KB", bytes / 1024.0);
    return b;
}

}  // namespace

float App::alto_banner_actualizacion() const { return banner_actualizacion.empty() ? 0.0f : BANNER_H + 6; }

// Chequea contra el manifiesto; con a_pedido tambien avisa si no hay nada.
void App::verificar_actualizacion(bool a_pedido) {
    actualizar::verificar(carpeta_exe(), [this, a_pedido] {
        actualizar::Estado e = actualizar::estado();
        if (e.hay) {
            banner_actualizacion = L"Update available (" + megas(e.total_bytes) + L")  —  click to install";
            banner_instalable = true;
        } else if (a_pedido) {
            banner_actualizacion = e.error.empty() ? L"kciwapp is up to date" : e.error;
            banner_instalable = false;
            SetTimer(hwnd, 12, 4000, nullptr);
        }
        pedir_dibujo();
    });
}

void App::dibujar_banner_actualizacion() {
    if (banner_actualizacion.empty()) return;
    float W = ancho_lista;
    bool inst = banner_instalable;
    g.rect_redondo(12, BANNER_Y, W - 24, BANNER_H, 8, Color(inst ? ACCENT() : BG_CAMPO(), inst ? 0.9f : 1.0f));
    if (inst) g.renglon_fuente(L"Segoe MDL2 Assets", L"", 24, BANNER_Y + 8, 14, Color(0xffffff));
    g.renglon(banner_actualizacion, inst ? 46 : 24, BANNER_Y + 7, 12.5f, Color(inst ? 0xffffff : TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - 60);
}

bool App::click_banner_actualizacion(float x, float y) {
    if (banner_actualizacion.empty() || x >= ancho_lista || y < BANNER_Y || y >= BANNER_Y + BANNER_H) return false;
    if (banner_instalable) empezar_actualizacion();
    else {
        banner_actualizacion.clear();
        KillTimer(hwnd, 12);
    }
    pedir_dibujo();
    return true;
}

bool App::sobre_banner_actualizacion(float x, float y) const {
    return !banner_actualizacion.empty() && x < ancho_lista && y >= BANNER_Y && y < BANNER_Y + BANNER_H;
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
                banner_actualizacion = actualizar::estado().error;
                banner_instalable = false;
                campo.foco = true;
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
