#include "toast.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <deque>
#include <map>
#include <set>

#include "gfx.h"
#include "red.h"
#include "tema.h"

namespace {

constexpr float ANCHO = 380.0f, ALTO_AVISO = 88.0f, SEPARACION = 8.0f, MARGEN = 16.0f;
constexpr int MAXIMO = 4;
const UINT_PTR TIMER_AVISOS = 7;

struct Aviso {
    std::wstring titulo, texto;
    std::string chat, mensaje;
    unsigned long long hasta = 0;
    std::string clave_foto;
};

HWND g_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
Gfx g_gfx;
bool g_gfx_lista = false;
std::deque<Aviso> g_avisos;
std::function<void(const std::string&, const std::string&)> g_al_click;
float g_mouse_x = -1, g_mouse_y = -1;
// Fotos ya bajadas (por clave), subidas al Gfx de esta ventana.
std::map<std::string, ComPtr<ID2D1Bitmap1>> g_fotos;
std::map<std::string, Pixeles> g_fotos_pendientes;
std::set<std::string> g_fotos_pedidas;

struct Monitor {
    HMONITOR h;
    RECT trabajo;
    std::wstring nombre;
};

std::vector<Monitor> lista_monitores() {
    std::vector<Monitor> r;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* v = (std::vector<Monitor>*)lp;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(h, &mi);
        std::wstring nombre = mi.szDevice;
        // Nombre amigable via EnumDisplayDevices (el del panel, no "\\\\.\\DISPLAY1").
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof dd;
        if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) nombre = dd.DeviceString;
        wchar_t buf[128];
        swprintf(buf, 128, L"%s (%dx%d)", nombre.c_str(), mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
        if (mi.dwFlags & MONITORINFOF_PRIMARY) wcscat_s(buf, L" - primary");
        v->push_back({h, mi.rcWork, buf});
        return TRUE;
    }, (LPARAM)&r);
    return r;
}

float alto_total() { return (float)g_avisos.size() * (ALTO_AVISO + SEPARACION) - SEPARACION + 2 * MARGEN; }

// Ubica la ventana en la esquina elegida del monitor elegido.
void ubicar() {
    std::vector<Monitor> ms = lista_monitores();
    if (ms.empty()) return;
    const Ajustes& a = ajustes::actual();
    int idx = a.monitor_avisos;
    if (idx < 0 || idx >= (int)ms.size()) {
        idx = 0;
        for (size_t i = 0; i < ms.size(); i++)
            if (ms[i].nombre.find(L"primary") != std::wstring::npos) idx = (int)i;
    }
    RECT t = ms[idx].trabajo;
    UINT dpi = GetDpiForWindow(g_hwnd);
    float e = dpi / 96.0f;
    int w = (int)((ANCHO + 2 * MARGEN) * e), h = (int)(alto_total() * e);
    int x = (a.esquina_avisos == 0 || a.esquina_avisos == 1) ? t.right - w : t.left;
    int y = (a.esquina_avisos == 0 || a.esquina_avisos == 2) ? t.bottom - h : t.top;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void pedir_foto(const std::string& clave, const std::wstring& ruta) {
    if (clave.empty() || g_fotos.count(clave) || g_fotos_pedidas.count(clave)) return;
    g_fotos_pedidas.insert(clave);
    red::en_fondo([clave, ruta] {
        Respuesta r = red::obtener(ruta, 20000);
        if (!r.ok()) return;
        Pixeles p = g_gfx.decodificar(r.cuerpo, false);
        if (p.vacio()) return;
        red::en_ui([clave, p] {
            g_fotos_pendientes[clave] = p;
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        });
    });
}

void dibujar() {
    if (!g_gfx_lista) return;
    // Fotos que llegaron: se suben con este contexto.
    for (auto& [k, p] : g_fotos_pendientes) g_fotos[k] = g_gfx.subir(p);
    g_fotos_pendientes.clear();
    Gfx& g = g_gfx;
    g.empezar_frame();
    g.ctx->Clear(Color(BG_APP()).d2d());
    float y = MARGEN;
    for (auto& av : g_avisos) {
        float x = MARGEN;
        bool encima = g_mouse_x >= x && g_mouse_x < x + ANCHO && g_mouse_y >= y && g_mouse_y < y + ALTO_AVISO;
        g.rect_redondo(x, y, ANCHO, ALTO_AVISO, 12, Color(encima ? BG_HOVER() : BG_PANEL()));
        g.borde_redondo(x, y, ANCHO, ALTO_AVISO, 12, Color(BORDE()), 1.0f);
        g.rect_redondo(x, y + 12, 4, ALTO_AVISO - 24, 2, Color(ACCENT()));
        float r = 26, cx = x + 20 + r, cy = y + ALTO_AVISO / 2;
        auto it = g_fotos.find(av.clave_foto);
        if (it != g_fotos.end() && it->second) g.bitmap_circular(it->second.Get(), cx, cy, r);
        else {
            g.circulo(cx, cy, r, Color(0x6b7c85));
            std::wstring inicial = av.titulo.empty() ? L"?" : av.titulo.substr(0, 1);
            float iw = g.medir(inicial, 22);
            g.renglon(inicial, cx - iw / 2, cy - 14, 22, Color(0xdfe5e7));
        }
        float tx = cx + r + 14;
        g.renglon(av.titulo, tx, y + 14, 15, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD, ANCHO - (tx - x) - 40);
        // El texto en hasta dos renglones.
        auto l = g.texto(av.texto, 13.5f, ANCHO - (tx - x) - 20, DWRITE_FONT_WEIGHT_NORMAL, 40);
        if (l) {
            DWRITE_TRIMMING recorte = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            l->SetTrimming(&recorte, nullptr);
            g.dibujar_texto(l.Get(), tx, y + 38, Color(TXT_DIM()));
        }
        g.renglon(L"✕", x + ANCHO - 30, y + 10, 14, Color(TXT_DIM()));
        y += ALTO_AVISO + SEPARACION;
    }
    g.terminar_frame();
}

LRESULT CALLBACK procedimiento(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            ValidateRect(h, nullptr);
            dibujar();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (g_gfx_lista) g_gfx.redimensionar();
            return 0;
        case WM_MOUSEMOVE: {
            float e = GetDpiForWindow(h) / 96.0f;
            g_mouse_x = GET_X_LPARAM(lp) / e;
            g_mouse_y = GET_Y_LPARAM(lp) / e;
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            g_mouse_x = g_mouse_y = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            float e = GetDpiForWindow(h) / 96.0f;
            float x = GET_X_LPARAM(lp) / e, y = GET_Y_LPARAM(lp) / e;
            float yy = MARGEN;
            for (size_t i = 0; i < g_avisos.size(); i++, yy += ALTO_AVISO + SEPARACION) {
                if (y < yy || y >= yy + ALTO_AVISO) continue;
                Aviso av = g_avisos[i];
                g_avisos.erase(g_avisos.begin() + i);
                bool cerrar = x >= MARGEN + ANCHO - 40 && y < yy + 34;
                if (g_avisos.empty()) ShowWindow(h, SW_HIDE);
                else ubicar();
                InvalidateRect(h, nullptr, FALSE);
                if (!cerrar && g_al_click) g_al_click(av.chat, av.mensaje);
                break;
            }
            return 0;
        }
        case WM_TIMER: {
            unsigned long long ahora = GetTickCount64();
            bool cambio = false;
            for (size_t i = 0; i < g_avisos.size();) {
                if (g_avisos[i].hasta <= ahora && !(g_mouse_x >= 0)) {
                    g_avisos.erase(g_avisos.begin() + i);
                    cambio = true;
                } else i++;
            }
            if (cambio) {
                if (g_avisos.empty()) ShowWindow(h, SW_HIDE);
                else ubicar();
                InvalidateRect(h, nullptr, FALSE);
            }
            if (g_avisos.empty()) KillTimer(h, TIMER_AVISOS);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void crear() {
    if (g_hwnd) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = procedimiento;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"kciwapp2-toast";
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"kciwapp2-toast", L"", WS_POPUP, 0, 0,
                             (int)(ANCHO + 2 * MARGEN), (int)ALTO_AVISO, nullptr, nullptr, g_inst, nullptr);
    // Esquinas redondeadas de la ventana (Win11) y fondo transparente por DWM.
    int esquina = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(g_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &esquina, sizeof esquina);
    g_gfx_lista = g_gfx.iniciar(g_hwnd);
}

}  // namespace

namespace toast {

void iniciar(HINSTANCE inst, std::function<void(const std::string&, const std::string&)> al_click) {
    g_inst = inst;
    g_al_click = std::move(al_click);
}

void mostrar(const std::wstring& titulo, const std::wstring& texto, const std::string& chat,
             const std::string& mensaje, const std::wstring& ruta_foto_http) {
    crear();
    if (!g_hwnd) return;
    Aviso av{titulo, texto, chat, mensaje, GetTickCount64() + (unsigned long long)std::max(2, ajustes::actual().segundos_aviso) * 1000};
    av.clave_foto = chat;
    if (!ruta_foto_http.empty()) pedir_foto(chat, ruta_foto_http);
    // El mismo chat, si ya tiene aviso, se reemplaza (no se apilan 20 del mismo).
    for (size_t i = 0; i < g_avisos.size(); i++)
        if (g_avisos[i].chat == chat) {
            g_avisos.erase(g_avisos.begin() + i);
            break;
        }
    g_avisos.push_back(av);
    while ((int)g_avisos.size() > MAXIMO) g_avisos.pop_front();
    ubicar();
    SetTimer(g_hwnd, TIMER_AVISOS, 250, nullptr);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

std::vector<std::wstring> monitores() {
    std::vector<std::wstring> r;
    for (auto& m : lista_monitores()) r.push_back(m.nombre);
    return r;
}

void cerrar_todos() {
    g_avisos.clear();
    if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
}

}  // namespace toast
