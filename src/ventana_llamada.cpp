#include "ventana_llamada.h"

#include <dwmapi.h>
#include <windowsx.h>

#include "gfx.h"
#include "red.h"
#include "tema.h"
#include "webwa.h"

namespace {

const UINT_PTR TIMER_TIC = 1;

HWND g_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
Gfx g_gfx;
bool g_gfx_lista = false;
std::wstring g_nombre, g_estado;
std::string g_chat;
unsigned long long g_desde = 0;
bool g_video = false;
bool g_web_encima = false;
std::function<void()> g_al_colgar;
float g_mx = -1, g_my = -1;
ComPtr<ID2D1Bitmap1> g_foto;
Pixeles g_foto_pendiente;
bool g_foto_pedida = false;
ID2D1DeviceContext* g_foto_ctx = nullptr;

constexpr float BOTON_W = 96, BOTON_H = 34;

float escala() { return GetDpiForWindow(g_hwnd) / 96.0f; }

// Rectangulos de los botones (DIPs).
void botones(float& mx, float& ex, float& by) {
    float W = g_gfx.ancho, H = g_gfx.alto;
    by = H - BOTON_H - 24;
    mx = W / 2 - BOTON_W - 8;
    ex = W / 2 + 8;
}

void dibujar() {
    if (!g_gfx_lista) return;
    if (!g_foto_pendiente.vacio() && (!g_foto || g_foto_ctx != g_gfx.ctx.Get())) {
        g_foto = g_gfx.subir(g_foto_pendiente);
        g_foto_ctx = g_gfx.ctx.Get();
    }
    Gfx& g = g_gfx;
    g.empezar_frame();
    g.ctx->Clear(Color(BG_APP()).d2d());
    float W = g.ancho, H = g.alto;
    float r = 56, cx = W / 2, cy = H * 0.36f;
    if (g_foto) g.bitmap_circular(g_foto.Get(), cx, cy, r);
    else {
        g.circulo(cx, cy, r, Color(0x6b7c85));
        std::wstring inicial = g_nombre.empty() ? L"?" : g_nombre.substr(0, 1);
        float iw = g.medir(inicial, 44);
        g.renglon(inicial, cx - iw / 2, cy - 28, 44, Color(0xdfe5e7));
    }
    float nw = g.medir(g_nombre, 22);
    g.renglon(g_nombre, cx - nw / 2, cy + r + 18, 22, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    std::wstring est = g_estado;
    if (est.empty() && g_desde) {
        unsigned long long s = (GetTickCount64() - g_desde) / 1000;
        wchar_t buf[32];
        swprintf(buf, 32, L"%llu:%02llu", s / 60, s % 60);
        est = buf;
    }
    std::wstring tipo = g_video ? L"Video call" : L"Voice call";
    float tw = g.medir(tipo, 13);
    g.renglon(tipo, cx - tw / 2, cy + r + 50, 13, Color(TXT_DIM()));
    float ew = g.medir(est, 15);
    g.renglon(est, cx - ew / 2, cy + r + 72, 15, Color(est == L"Calling..." || est == L"Connecting..." ? TXT_DIM() : ACCENT()));
    // Mute / End.
    float mx, ex, by;
    botones(mx, ex, by);
    bool mudo = webwa::silenciado();
    g.rect_redondo(mx, by, BOTON_W, BOTON_H, 8, Color(mudo ? ACCENT() : BG_CAMPO()));
    std::wstring mt = mudo ? L"Unmute" : L"Mute";
    tw = g.medir(mt, 14);
    g.renglon(mt, mx + (BOTON_W - tw) / 2, by + 8, 14, Color(mudo ? 0xffffff : TXT()));
    g.rect_redondo(ex, by, BOTON_W, BOTON_H, 8, Color(0xf15c6d));
    tw = g.medir(L"End", 14);
    g.renglon(L"End", ex + (BOTON_W - tw) / 2, by + 8, 14, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.terminar_frame();
}

void pedir_foto(const std::wstring& ruta) {
    if (ruta.empty() || g_foto_pedida) return;
    g_foto_pedida = true;
    red::en_fondo([ruta] {
        Respuesta r = red::obtener(ruta, 20000);
        if (!r.ok()) return;
        Pixeles p = g_gfx.decodificar(r.cuerpo, false);
        if (p.vacio()) return;
        red::en_ui([p] {
            g_foto_pendiente = p;
            g_foto.Reset();
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        });
    });
}

// La vista de WhatsApp Web encima (video llegando) o escondida.
void acomodar_web() {
    if (!g_hwnd) return;
    bool encima = g_video && webwa::video_fluye();
    if (encima) {
        RECT c;
        GetClientRect(g_hwnd, &c);
        webwa::poner_vista(g_hwnd, &c);
    } else if (g_web_encima) {
        webwa::poner_vista(nullptr, nullptr);
    }
    g_web_encima = encima;
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
            acomodar_web();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SIZING: {
            // Con video se mantiene la proporcion de la ventanita de WhatsApp.
            if (!g_video) break;
            RECT* r = (RECT*)lp;
            RECT wr, cr;
            GetWindowRect(h, &wr);
            GetClientRect(h, &cr);
            int extra_w = (wr.right - wr.left) - cr.right, extra_h = (wr.bottom - wr.top) - cr.bottom;
            double prop = webwa::proporcion_video();
            int w = (r->right - r->left) - extra_w, hh = (r->bottom - r->top) - extra_h;
            bool por_alto = wp == WMSZ_TOP || wp == WMSZ_BOTTOM;
            if (por_alto) w = (int)(hh * prop + 0.5);
            else hh = (int)(w / prop + 0.5);
            if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT) r->left = r->right - (w + extra_w);
            else r->right = r->left + (w + extra_w);
            if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r->top = r->bottom - (hh + extra_h);
            else r->bottom = r->top + (hh + extra_h);
            return TRUE;
        }
        case WM_TIMER:
            if (wp == TIMER_TIC) {
                acomodar_web();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            float e = escala();
            g_mx = GET_X_LPARAM(lp) / e;
            g_my = GET_Y_LPARAM(lp) / e;
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                float mx, ex, by;
                botones(mx, ex, by);
                bool sobre = g_my >= by && g_my < by + BOTON_H && ((g_mx >= mx && g_mx < mx + BOTON_W) || (g_mx >= ex && g_mx < ex + BOTON_W));
                SetCursor(LoadCursor(nullptr, sobre ? IDC_HAND : IDC_ARROW));
                return 1;
            }
            break;
        case WM_LBUTTONDOWN: {
            float e = escala();
            float x = GET_X_LPARAM(lp) / e, y = GET_Y_LPARAM(lp) / e;
            float mx, ex, by;
            botones(mx, ex, by);
            if (y >= by && y < by + BOTON_H) {
                if (x >= ex && x < ex + BOTON_W && g_al_colgar) g_al_colgar();
                else if (x >= mx && x < mx + BOTON_W) webwa::silenciar(!webwa::silenciado());
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CLOSE:
            if (g_al_colgar) g_al_colgar();
            return 0;
        case WM_DESTROY:
            g_hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace vllamada {

void abrir(HINSTANCE inst, const std::wstring& nombre, const std::string& chat, const std::wstring& ruta_foto_http, bool video) {
    g_inst = inst;
    g_nombre = nombre;
    g_chat = chat;
    g_video = video;
    g_estado = L"Calling...";
    g_desde = 0;
    g_foto.Reset();
    g_foto_pendiente = Pixeles();
    g_foto_pedida = false;
    g_web_encima = false;
    if (!g_hwnd) {
        static bool registrada = false;
        if (!registrada) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = procedimiento;
            wc.hInstance = inst;
            wc.lpszClassName = L"kciwapp2-llamada";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
            RegisterClassW(&wc);
            registrada = true;
        }
        HWND principal = GetActiveWindow();
        RECT rp{0, 0, 1200, 800};
        if (principal) GetWindowRect(principal, &rp);
        UINT dpi = principal ? GetDpiForWindow(principal) : 96;
        int w = MulDiv(video ? 1000 : 380, dpi, 96), h = MulDiv(video ? 760 : 520, dpi, 96);
        int x = rp.left + ((rp.right - rp.left) - w) / 2, y = rp.top + ((rp.bottom - rp.top) - h) / 2;
        g_hwnd = CreateWindowExW(0, L"kciwapp2-llamada", video ? L"Video call" : L"Voice call", WS_OVERLAPPEDWINDOW, x, y, w, h,
                                 nullptr, nullptr, inst, nullptr);
        BOOL oscuro = TRUE;
        DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &oscuro, sizeof oscuro);
        g_gfx_lista = g_gfx.iniciar(g_hwnd);
    } else {
        SetWindowTextW(g_hwnd, video ? L"Video call" : L"Voice call");
    }
    pedir_foto(ruta_foto_http);
    ShowWindow(g_hwnd, SW_SHOW);
    SetTimer(g_hwnd, TIMER_TIC, 250, nullptr);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void cerrar() {
    if (!g_hwnd) return;
    if (g_web_encima) webwa::poner_vista(nullptr, nullptr);
    g_web_encima = false;
    KillTimer(g_hwnd, TIMER_TIC);
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    g_gfx_lista = false;
    g_foto.Reset();
    g_foto_pendiente = Pixeles();
    g_gfx.~Gfx();
    new (&g_gfx) Gfx();
}

bool abierta() { return g_hwnd != nullptr; }

void estado(const std::wstring& texto, unsigned long long desde) {
    // "Calling..." / "Connecting..." hasta que atienden; desde ahi, el tiempo.
    g_estado = texto;
    g_desde = desde;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

void al_colgar(std::function<void()> f) { g_al_colgar = std::move(f); }

}  // namespace vllamada
