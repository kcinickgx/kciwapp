// kciwapp2: cliente de WhatsApp para Windows, Win32 + Direct2D. Habla con
// kciwapp-server (la VM) por HTTP/JSON.
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <cstdio>


#include "app.h"
#include "red.h"

namespace {

App* g_app = nullptr;
const UINT_PTR TIMER_CURSOR = 1;

std::wstring carpeta_datos() {
    std::wstring r = carpeta_exe() + L"\\datos";
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
}

// servidor.json al lado del exe: {"host": "...", "puerto": 8080, "token": "..."}
std::wstring ruta_config() { return carpeta_exe() + L"\\servidor.json"; }

bool leer_config() {
    HANDLE h = CreateFileW(ruta_config().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string s;
    char buf[4096];
    DWORD leido = 0;
    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) s.append(buf, leido);
    CloseHandle(h);
    Json j = Json::parsear(s);
    red::configurar(ancho(j["host"].str()), (int)j["puerto"].entero(8080), j["token"].str());
    return red::configurado();
}

float escala(HWND h) { return GetDpiForWindow(h) / 96.0f; }

LRESULT CALLBACK ventana(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = g_app;
    switch (msg) {
        case WM_CREATE: {
            BOOL oscuro = TRUE;
            DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &oscuro, sizeof oscuro);
            return 0;
        }
        case WM_SIZE:
            if (app && wp != SIZE_MINIMIZED) {
                app->redimensionado();
                app->dibujar();
            }
            return 0;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            float e = escala(h);
            mm->ptMinTrackSize.x = (LONG)(720 * e);
            mm->ptMinTrackSize.y = (LONG)(480 * e);
            return 0;
        }
        case WM_PAINT: {
            ValidateRect(h, nullptr);
            if (app) app->pedir_dibujo();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
            if (app) {
                float e = escala(h);
                app->raton_mueve(GET_X_LPARAM(lp) / e, GET_Y_LPARAM(lp) / e);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (app) {
                float e = escala(h);
                app->raton_abajo(GET_X_LPARAM(lp) / e, GET_Y_LPARAM(lp) / e, (wp & MK_SHIFT) != 0);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if (app) {
                float e = escala(h);
                app->doble_click(GET_X_LPARAM(lp) / e, GET_Y_LPARAM(lp) / e);
            }
            return 0;
        case WM_RBUTTONUP:
            if (app) {
                float e = escala(h);
                app->raton_derecho(GET_X_LPARAM(lp) / e, GET_Y_LPARAM(lp) / e);
            }
            return 0;
        case WM_DROPFILES:
            if (app) app->soltar_archivos((HDROP)wp);
            return 0;
        case WM_LBUTTONUP:
            if (app) {
                float e = escala(h);
                app->raton_arriba(GET_X_LPARAM(lp) / e, GET_Y_LPARAM(lp) / e);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (app) {
                POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(h, &p);
                float e = escala(h);
                app->rueda(p.x / e, p.y / e, (float)GET_WHEEL_DELTA_WPARAM(wp));
            }
            return 0;
        case WM_KEYDOWN:
            if (app) app->tecla(wp, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
            return 0;
        case WM_CHAR:
            if (app) app->caracter((wchar_t)wp);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return 1;
            }
            break;
        case WM_TIMER:
            if (wp == TIMER_CURSOR && app && app->campo.foco) app->pedir_dibujo();
            if (wp == 2 && app) {
                KillTimer(h, 2);
                app->cargar_chats();
            }
            if (wp == 3 && app) {
                KillTimer(h, 3);
                app->buscar_ahora();
            }
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (app) app->pedir_dibujo();
            return 0;
        case red::WM_TRABAJO:
            red::atender_trabajo(lp);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

static LONG WINAPI caida(EXCEPTION_POINTERS* e) {
    char buf[200];
    HMODULE base = GetModuleHandleW(nullptr);
    snprintf(buf, sizeof buf, "CRASH codigo=%08lx direccion=+%llx", e->ExceptionRecord->ExceptionCode,
             (unsigned long long)((char*)e->ExceptionRecord->ExceptionAddress - (char*)base));
    red::registrar(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetUnhandledExceptionFilter(caida);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!leer_config()) {
        std::wstring m = L"Missing or invalid " + ruta_config() +
                         L"\n\n{\"host\": \"192.168.5.15\", \"puerto\": 8080, \"token\": \"...\"}";
        MessageBoxW(nullptr, m.c_str(), L"kciwapp", MB_ICONERROR);
        return 1;
    }

    // Menus contextuales oscuros: SetPreferredAppMode(ForceDark) de uxtheme
    // (ordinal 135, sin documentar pero estable desde 1809).
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        using SetPreferredAppMode = int(WINAPI*)(int);
        if (auto f = (SetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135))) f(2);
        if (auto flush = (void(WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(136))) flush();
    }

    WNDCLASSEXW wc = {sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ventana;
    wc.hInstance = inst;
    wc.lpszClassName = L"kciwapp2";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    App app;
    g_app = nullptr;
    HWND h = CreateWindowExW(0, L"kciwapp2", L"kciwapp", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             (int)(1100 * 1.0f), (int)(760 * 1.0f), nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    red::anotar_ventana(h);
    DragAcceptFiles(h, TRUE);
    app.iniciar(h);
    g_app = &app;
    SetTimer(h, TIMER_CURSOR, 500, nullptr);
    ShowWindow(h, SW_SHOW);

    MSG msg;
    for (;;) {
        bool anim = app.animando();
        if (anim || app.necesita_dibujar) {
            bool salir = false;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { salir = true; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (salir) break;
            // Un frame por refresco del monitor: el swap chain avisa cuando
            // hay lugar para el siguiente.
            if (app.g.espera_frame) WaitForSingleObjectEx(app.g.espera_frame, 100, TRUE);
            app.dibujar();
        } else {
            BOOL r = GetMessageW(&msg, nullptr, 0, 0);
            if (r <= 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_app = nullptr;
    return 0;
}
