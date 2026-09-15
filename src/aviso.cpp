#include "aviso.h"

#include <shellapi.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace aviso {
namespace {

// Mensajes propios de la ventana oculta (no colisiona con los de la ventana
// principal: son ventanas distintas, cada una con su WndProc).
const UINT WM_BANDEJA = WM_APP + 1;
const UINT WM_MOSTRAR_GLOBO = WM_APP + 2;
const UINT WM_TERMINAR = WM_APP + 3;

enum { ID_ABRIR = 1, ID_SALIR = 2 };

// Lo que se manda por WM_MOSTRAR_GLOBO: se crea con new y el hilo de la
// bandeja lo destruye despues de usarlo.
struct Globo {
    std::wstring titulo, texto;
};

HICON g_icono = nullptr;
std::atomic<HWND> g_ventana{nullptr};
std::atomic<HWND> g_principal{nullptr};
std::atomic<bool> g_arrancado{false};
std::thread g_hilo;

std::mutex g_mutex_destino;
std::optional<Destino> g_ultimo_destino;  // a que lleva el ultimo globo mostrado

std::mutex g_mutex_callback;
std::function<void(Destino)> g_callback;

void copiar_truncado(wchar_t* destino, size_t capacidad, const std::wstring& s) {
    size_t n = s.size() < capacidad - 1 ? s.size() : capacidad - 1;
    wmemcpy(destino, s.c_str(), n);
    destino[n] = 0;
}

void traer_al_frente(HWND h) {
    if (!h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
}

void manejar_click_globo() {
    traer_al_frente(g_principal.load());
    Destino d;
    bool hay = false;
    {
        std::lock_guard<std::mutex> lk(g_mutex_destino);
        if (g_ultimo_destino) {
            d = *g_ultimo_destino;
            hay = true;
        }
    }
    if (!hay) return;
    std::function<void(Destino)> cb;
    {
        std::lock_guard<std::mutex> lk(g_mutex_callback);
        cb = g_callback;
    }
    if (cb) cb(d);
}

void mostrar_menu(HWND h) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_ABRIR, L"Open");
    AppendMenuW(menu, MF_STRING, ID_SALIR, L"Quit");
    // El truco de siempre para que el menu se cierre bien si el usuario
    // clickea afuera: la ventana tiene que estar al frente antes de
    // TrackPopupMenu, y despues se le manda un mensaje nulo.
    SetForegroundWindow(h);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
    PostMessageW(h, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void mostrar_globo_interno(HWND h, const Globo& gl) {
    NOTIFYICONDATAW datos{};
    datos.cbSize = sizeof datos;
    datos.hWnd = h;
    datos.uID = 1;
    datos.uFlags = NIF_INFO | NIF_ICON;
    datos.hIcon = g_icono;
    datos.dwInfoFlags = NIIF_USER;
    datos.hBalloonIcon = g_icono;
    copiar_truncado(datos.szInfoTitle, 64, gl.titulo);
    copiar_truncado(datos.szInfo, 256, gl.texto);
    Shell_NotifyIconW(NIM_MODIFY, &datos);
}

LRESULT CALLBACK procedimiento(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_BANDEJA: {
            // Con la version 4 del icono el evento viene en la palabra baja
            // del lParam (los mensajes de mouse de siempre, mas los NIN_*
            // nuevos para teclado/globo).
            switch (LOWORD(lp)) {
                case WM_LBUTTONDBLCLK:
                    traer_al_frente(g_principal.load());
                    break;
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP:
                    mostrar_menu(h);
                    break;
                case NIN_BALLOONUSERCLICK:
                    manejar_click_globo();
                    break;
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_ABRIR) {
                traer_al_frente(g_principal.load());
            } else if (LOWORD(wp) == ID_SALIR) {
                HWND p = g_principal.load();
                if (p) PostMessageW(p, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_MOSTRAR_GLOBO: {
            auto* gl = (Globo*)lp;
            mostrar_globo_interno(h, *gl);
            delete gl;
            return 0;
        }
        case WM_TERMINAR:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void arrancar(HICON icono, const std::wstring& titulo_app) {
    if (g_arrancado.exchange(true)) return;
    g_icono = icono;
    g_hilo = std::thread([titulo_app] {
        HINSTANCE instancia = GetModuleHandleW(nullptr);
        WNDCLASSW clase{};
        clase.lpfnWndProc = procedimiento;
        clase.hInstance = instancia;
        clase.lpszClassName = L"kciwapp2-aviso";
        RegisterClassW(&clase);
        HWND v = CreateWindowExW(0, L"kciwapp2-aviso", titulo_app.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  instancia, nullptr);
        if (!v) {
            g_arrancado.store(false);
            return;
        }

        NOTIFYICONDATAW datos{};
        datos.cbSize = sizeof datos;
        datos.hWnd = v;
        datos.uID = 1;
        datos.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        datos.uCallbackMessage = WM_BANDEJA;
        datos.hIcon = g_icono;
        copiar_truncado(datos.szTip, 128, titulo_app);
        datos.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_ADD, &datos);
        Shell_NotifyIconW(NIM_SETVERSION, &datos);

        g_ventana.store(v);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        Shell_NotifyIconW(NIM_DELETE, &datos);
        g_ventana.store(nullptr);
    });
}

void parar() {
    HWND v = g_ventana.load();
    if (v) PostMessageW(v, WM_TERMINAR, 0, 0);
    if (g_hilo.joinable()) g_hilo.join();
    g_arrancado.store(false);
}

void mostrar(const std::wstring& titulo, const std::wstring& texto, const std::string& chat,
             const std::string& mensaje_id) {
    HWND v = g_ventana.load();
    if (!v) return;
    {
        std::lock_guard<std::mutex> lk(g_mutex_destino);
        g_ultimo_destino = Destino{chat, mensaje_id};
    }
    auto* gl = new Globo{titulo, texto};
    if (!PostMessageW(v, WM_MOSTRAR_GLOBO, 0, (LPARAM)gl)) delete gl;
}

void anotar_principal(HWND h) { g_principal.store(h); }

bool esta_al_frente(HWND h) { return h && GetForegroundWindow() == h && !IsIconic(h); }

void al_click(std::function<void(Destino)> f) {
    std::lock_guard<std::mutex> lk(g_mutex_callback);
    g_callback = std::move(f);
}

}  // namespace aviso
