// kciwapp2: cliente de WhatsApp para Windows, Win32 + Direct2D. Habla con
// kciwapp-server (la VM) por HTTP/JSON.
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <cstdio>


#include "app.h"
#include "aviso.h"
#include "cache.h"
#include "emoji.h"
#include "red.h"
#include "tema.h"
#include "toast.h"
#include "ventana_llamada.h"
#include "webwa.h"

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

// Posicion y tamano de la ventana en ajustes.json, para que vuelva a abrir
// donde quedo (y maximizada si lo estaba).
bool g_ventana_lista = false;  // hasta restaurar, los WM_SIZE de la creacion no cuentan
bool g_era_max = false;

void guardar_ventana(HWND h) {
    if (!g_ventana_lista) return;
    WINDOWPLACEMENT wp{sizeof wp};
    if (!GetWindowPlacement(h, &wp) || IsIconic(h)) return;
    RECT r = wp.rcNormalPosition;
    if (r.right - r.left < 200 || r.bottom - r.top < 200) return;
    const Ajustes& a = ajustes::actual();
    bool max = wp.showCmd == SW_SHOWMAXIMIZED;
    if (a.ventana_x == r.left && a.ventana_y == r.top && a.ventana_w == r.right - r.left && a.ventana_h == r.bottom - r.top && a.ventana_max == max) return;
    ajustes::cambiar([&](Ajustes& x) {
        x.ventana_x = r.left;
        x.ventana_y = r.top;
        x.ventana_w = r.right - r.left;
        x.ventana_h = r.bottom - r.top;
        x.ventana_max = max;
    });
}

void restaurar_ventana(HWND h) {
    const Ajustes& a = ajustes::actual();
    if (a.ventana_w <= 0 || a.ventana_h <= 0) return;
    RECT r{a.ventana_x, a.ventana_y, a.ventana_x + a.ventana_w, a.ventana_y + a.ventana_h};
    // Si el monitor donde estaba ya no existe, se deja donde Windows la ponga.
    if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return;
    WINDOWPLACEMENT wp{sizeof wp};
    GetWindowPlacement(h, &wp);
    wp.rcNormalPosition = r;
    wp.showCmd = SW_HIDE;  // la muestra ShowWindow despues (normal o maximizada)
    SetWindowPlacement(h, &wp);
}

LRESULT CALLBACK ventana(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = g_app;
    switch (msg) {
        case WM_CREATE: {
            BOOL oscuro = TRUE;
            DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &oscuro, sizeof oscuro);
            return 0;
        }
        case WM_SIZE:
            // Maximizar/restaurar no pasa por EXITSIZEMOVE.
            if (wp == SIZE_MAXIMIZED || (wp == SIZE_RESTORED && g_era_max)) guardar_ventana(h);
            if (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) g_era_max = wp == SIZE_MAXIMIZED;
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
                SetCursor(LoadCursor(nullptr, app && app->cursor_mano ? IDC_HAND : (app && app->cursor_texto ? IDC_IBEAM : IDC_ARROW)));
                return 1;
            }
            break;
        case WM_TIMER:
            if (wp == TIMER_CURSOR && app && (app->campo.foco || app->buscador_chat.foco)) app->pedir_dibujo();
            if (wp == 2 && app) {
                KillTimer(h, 2);
                app->cargar_chats();
            }
            if (wp == 3 && app) {
                KillTimer(h, 3);
                app->buscar_ahora();
            }
            if (wp == 5 && app) {
                KillTimer(h, 5);
                app->buscar_en_chat();
            }
            if (wp == 7 && app) {
                KillTimer(h, 7);
                // Recarga del chat abierto tras "historia": una sola, y recien
                // cuando no hay otra carga en curso.
                if (app->refresco_pendiente && !app->chat_actual.empty()) {
                    if (app->cargando_mensajes) SetTimer(h, 7, 3000, nullptr);
                    else {
                        app->refresco_pendiente = false;
                        app->refrescar_chat_del_server(app->chat_actual);
                    }
                }
            }
            if (wp == 8 && app) {
                // Cada segundo: el reloj de la llamada; cada dos, el sondeo de WhatsApp Web.
                static int tic = 0;
                if ((++tic & 1) == 0 && webwa::activo()) webwa::sondear();
                if (app->llamada_activa) app->pedir_dibujo();
            }
            if (wp == 6 && app) {
                KillTimer(h, 6);
                // Dejamos de teclear: paused (salvo que estemos grabando).
                if (app->presencia_mandada == "typing") app->mandar_presencia("");
            }
            if (wp == 4 && app) {
                KillTimer(h, 4);
                app->pedir_dibujo();
            }
            return 0;
        case WM_SETFOCUS:
            if (app) {
                app->ventana_activada();
                app->pedir_dibujo();
            }
            return 0;
        case WM_KILLFOCUS:
            if (app) app->pedir_dibujo();
            return 0;
        case red::WM_TRABAJO:
            red::atender_trabajo(lp);
            return 0;
        case WM_EXITSIZEMOVE:
            guardar_ventana(h);
            return 0;
        case WM_DESTROY:
            guardar_ventana(h);
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
    // Una sola instancia: si ya hay una, se la trae al frente y listo (dos
    // procesos se pisarian la cache y el cursor de eventos).
    HANDLE unica = CreateMutexW(nullptr, TRUE, L"Local\kciwapp2-instancia");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND otra = FindWindowW(L"kciwapp2", nullptr)) {
            if (IsIconic(otra)) ShowWindow(otra, SW_RESTORE);
            SetForegroundWindow(otra);
        }
        return 0;
    }
    (void)unica;
    ajustes::cargar(carpeta_exe());
    HWND h = CreateWindowExW(0, L"kciwapp2", L"kciwapp", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             (int)(1100 * 1.0f), (int)(760 * 1.0f), nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    restaurar_ventana(h);
    g_ventana_lista = true;
    red::anotar_ventana(h);
    DragAcceptFiles(h, TRUE);
    cache::abrir(carpeta_datos() + L"\\cache.sqlite3");
    emoji::cargar(carpeta_exe());
    app.iniciar(h);
    g_app = &app;
    // Bandeja y globos de notificacion; el click en un globo abre ese mensaje.
    toast::al_atender([](const std::string& id) {
        if (!g_app) return;
        auto it = g_app->llamadas_entrantes.find(id);
        if (it == g_app->llamadas_entrantes.end()) return;
        HWND h = g_app->hwnd;
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        g_app->atender_llamada(it->second.first, it->second.second);
    });
    webwa::al_cambiar_llamada([](bool en) {
        if (g_app) g_app->llamada_cambio(en);
    });
    vllamada::al_colgar([]() {
        if (g_app) g_app->colgar_llamada();
    });
    SetTimer(h, 8, 1000, nullptr);
    toast::al_rechazar([](const std::string& id) {
        std::string cuerpo = "{\"id\":" + json_texto(id) + "}";
        red::en_fondo([cuerpo] { red::mandar_json(L"/llamada/rechazar", cuerpo); });
    });
    toast::iniciar(inst, [](const std::string& chat, const std::string& mensaje) {
        if (!g_app) return;
        HWND h = g_app->hwnd;
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        long long ts = 0;
        for (auto& c : g_app->chats)
            if (c.jid == chat && c.ultimo && c.ultimo->id == mensaje) ts = c.ultimo->ts;
        if (ts) g_app->ir_a_mensaje(chat, mensaje, ts);
        else g_app->abrir_chat(chat);
    });
    aviso::arrancar(wc.hIcon, L"kciwapp");
    aviso::anotar_principal(h);
    aviso::al_click([](aviso::Destino d) {
        red::en_ui([d] {
            if (!g_app) return;
            long long ts = 0;
            for (auto& c : g_app->chats)
                if (c.jid == d.chat && c.ultimo && c.ultimo->id == d.mensaje) ts = c.ultimo->ts;
            if (ts) g_app->ir_a_mensaje(d.chat, d.mensaje, ts);
            else g_app->abrir_chat(d.chat);
        });
    });
    SetTimer(h, TIMER_CURSOR, 250, nullptr);
    ShowWindow(h, ajustes::actual().ventana_max ? SW_SHOWMAXIMIZED : SW_SHOW);

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
    aviso::parar();
    cache::cerrar();
    return 0;
}
