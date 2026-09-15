#include "webwa.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <wrl.h>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "json.h"
#include "red.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

namespace {

typedef HRESULT(STDAPICALLTYPE* PFN_CrearEntorno)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

// Cada WebView2 vive en una ventana hija de la principal, fuera de la vista.
struct Vista {
    HWND hwnd = nullptr;
    ComPtr<ICoreWebView2Controller> ctrl;
    ComPtr<ICoreWebView2> web;
    bool creando = false;
    bool visible = false;  // si esta puesta sobre la conversacion (video)
};

constexpr int ANCHO_PX = 1100, ALTO_PX = 760;
constexpr int ESCONDIDO = -20000;

HMODULE g_dll = nullptr;
HWND g_padre = nullptr;
ComPtr<ICoreWebView2Environment> g_env;
Vista g_principal, g_llamada;
bool g_activo = false, g_logueado = false, g_cargando = true, g_en_llamada = false, g_silenciado = false;
std::string g_qr;
std::function<void(bool)> g_al_cambiar;
std::function<void()> g_al_cerrar_ventana;
HWND g_ventana = nullptr;  // la ventana propia de la videollamada
bool g_sondeando = false;

// Llamada pedida y todavia no concretada: se intenta apretar el boton en
// cada sondeo hasta que aparezca (la pagina tarda en abrir el chat).
struct Pendiente {
    bool hay = false;
    bool video = false;
    int intentos = 0;
    bool menu_abierto = false;
} g_pendiente;

void registrar(const std::string& s) { red::registrar("webwa: " + s); }

std::string base64_decodificar(const std::string& s) {
    static int tabla[256];
    static bool lista = false;
    if (!lista) {
        for (int i = 0; i < 256; i++) tabla[i] = -1;
        const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) tabla[(unsigned char)abc[i]] = i;
        lista = true;
    }
    std::string r;
    int val = 0, bits = -8;
    for (unsigned char c : s) {
        if (tabla[c] < 0) continue;
        val = (val << 6) + tabla[c];
        bits += 6;
        if (bits >= 0) {
            r.push_back((char)((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return r;
}

// Escapa una cadena para meterla en un literal JS entre comillas simples.
std::wstring js_literal(const std::wstring& s) {
    std::wstring r = L"'";
    for (wchar_t c : s) {
        if (c == L'\\' || c == L'\'') r += L'\\';
        if (c == L'\n') { r += L"\\n"; continue; }
        r += c;
    }
    return r + L"'";
}

void ejecutar(Vista& v, const std::wstring& script, std::function<void(const std::wstring&)> cb = nullptr) {
    if (!v.web) {
        if (cb) cb(L"");
        return;
    }
    v.web->ExecuteScript(script.c_str(),
                         Callback<ICoreWebView2ExecuteScriptCompletedHandler>([cb](HRESULT hr, LPCWSTR res) -> HRESULT {
                             if (cb) cb(SUCCEEDED(hr) && res ? std::wstring(res) : L"");
                             return S_OK;
                         }).Get());
}

// El resultado de ExecuteScript viene como JSON (un string JSON entre
// comillas); esto devuelve el string de adentro.
std::string resultado_str(const std::wstring& res) {
    Json j = Json::parsear(angosto(res));
    return j.str();
}

LRESULT CALLBACK proc_hija(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) {
        Vista* v = (Vista*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (v && v->ctrl) {
            RECT r;
            GetClientRect(h, &r);
            v->ctrl->put_Bounds(r);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND crear_hija(Vista& v) {
    static bool registrada = false;
    if (!registrada) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc_hija;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"kciwapp2-web";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        registrada = true;
    }
    HWND h = CreateWindowExW(0, L"kciwapp2-web", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, ESCONDIDO, ESCONDIDO, ANCHO_PX, ALTO_PX,
                             g_padre, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&v);
    return h;
}

void avisar_llamada(bool en) {
    if (en == g_en_llamada) return;
    g_en_llamada = en;
    if (!en) g_silenciado = false;
    registrar(en ? "llamada en curso" : "llamada terminada");
    if (g_al_cambiar) g_al_cambiar(en);
}

void configurar_permisos(Vista& v) {
    EventRegistrationToken t;
    v.web->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            COREWEBVIEW2_PERMISSION_KIND k;
            args->get_PermissionKind(&k);
            // Microfono y camara si; avisos del navegador no (los nuestros ya estan).
            if (k == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE || k == COREWEBVIEW2_PERMISSION_KIND_CAMERA)
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
            else if (k == COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS)
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
            return S_OK;
        }).Get(), &t);
    ComPtr<ICoreWebView2Settings> s;
    if (SUCCEEDED(v.web->get_Settings(&s)) && s) {
        s->put_AreDefaultContextMenusEnabled(FALSE);
        s->put_IsStatusBarEnabled(FALSE);
        s->put_IsZoomControlEnabled(FALSE);
    }
}

void cerrar_vista(Vista& v) {
    if (v.visible && g_ventana) {
        DestroyWindow(g_ventana);
        g_ventana = nullptr;
    }
    if (v.ctrl) v.ctrl->Close();
    v.ctrl.Reset();
    v.web.Reset();
    if (v.hwnd) DestroyWindow(v.hwnd);
    v.hwnd = nullptr;
    v.creando = false;
    v.visible = false;
}

// Crea el WebView2 de la llamada (la ventanita que WhatsApp Web abre con
// window.open) y cuando esta, `listo`.
void crear_vista_llamada(std::function<void()> listo) {
    if (g_llamada.web) {
        listo();
        return;
    }
    if (g_llamada.creando) return;
    g_llamada.creando = true;
    if (!g_llamada.hwnd) g_llamada.hwnd = crear_hija(g_llamada);
    g_env->CreateCoreWebView2Controller(
        g_llamada.hwnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([listo](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            g_llamada.creando = false;
            if (FAILED(hr) || !c) {
                registrar("no se pudo crear la vista de la llamada");
                return S_OK;
            }
            g_llamada.ctrl = c;
            c->get_CoreWebView2(&g_llamada.web);
            RECT r;
            GetClientRect(g_llamada.hwnd, &r);
            c->put_Bounds(r);
            c->put_IsVisible(TRUE);
            configurar_permisos(g_llamada);
            EventRegistrationToken t;
            // WhatsApp Web cierra la ventanita al cortar.
            g_llamada.web->add_WindowCloseRequested(
                Callback<ICoreWebView2WindowCloseRequestedEventHandler>([](ICoreWebView2*, IUnknown*) -> HRESULT {
                    registrar("ventana de llamada cerrada");
                    cerrar_vista(g_llamada);
                    avisar_llamada(false);
                    return S_OK;
                }).Get(), &t);
            listo();
            return S_OK;
        }).Get());
}

void crear_vista_principal() {
    if (!g_principal.hwnd) g_principal.hwnd = crear_hija(g_principal);
    g_principal.creando = true;
    g_env->CreateCoreWebView2Controller(
        g_principal.hwnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            g_principal.creando = false;
            if (FAILED(hr) || !c) {
                registrar("no se pudo crear el WebView2 principal");
                g_cargando = false;
                return S_OK;
            }
            g_principal.ctrl = c;
            c->get_CoreWebView2(&g_principal.web);
            RECT r;
            GetClientRect(g_principal.hwnd, &r);
            c->put_Bounds(r);
            c->put_IsVisible(TRUE);
            configurar_permisos(g_principal);
            EventRegistrationToken t;
            // window.open (la llamada) va a nuestra segunda vista, no a una
            // ventana suelta del navegador.
            g_principal.web->add_NewWindowRequested(
                Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    ComPtr<ICoreWebView2Deferral> def;
                    args->GetDeferral(&def);
                    ComPtr<ICoreWebView2NewWindowRequestedEventArgs> a = args;
                    registrar("window.open -> vista de llamada");
                    crear_vista_llamada([a, def]() {
                        a->put_NewWindow(g_llamada.web.Get());
                        a->put_Handled(TRUE);
                        def->Complete();
                    });
                    return S_OK;
                }).Get(), &t);
            g_principal.web->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    g_cargando = false;
                    return S_OK;
                }).Get(), &t);
            g_principal.web->Navigate(L"https://web.whatsapp.com/");
            return S_OK;
        }).Get());
}

// Aprieta el primer elemento que matchee alguno de los selectores.
std::wstring js_click(const std::vector<std::wstring>& etiquetas) {
    std::wstring sel;
    for (auto& e : etiquetas) {
        if (!sel.empty()) sel += L",";
        sel += L"[aria-label=" + js_literal(e) + L"],[title=" + js_literal(e) + L"],[data-icon=" + js_literal(e) + L"]";
    }
    // Tambien botones cuyo texto sea exactamente la etiqueta.
    std::wstring textos;
    for (auto& e : etiquetas) {
        if (!textos.empty()) textos += L",";
        textos += js_literal(e);
    }
    return L"(function(){var b=document.querySelector(" + js_literal(sel) + L");"
           L"if(!b){var ts=[" + textos + L"];var bs=document.querySelectorAll('button,div[role=button],span[role=button]');"
           L"for(var i=0;i<bs.length&&!b;i++){var t=(bs[i].innerText||'').trim();if(ts.indexOf(t)>=0)b=bs[i];}}"
           L"if(!b)return 'no';b.click();return 'ok'})()";
}

// Estado de la pagina principal: si esta logueado, y el QR si no.
const wchar_t* JS_ESTADO =
    L"(function(){var side=document.querySelector('#pane-side')||document.querySelector('[aria-label=\"Chat list\"]')||document.querySelector('[data-testid=\"chat-list\"]');"
    L"var qr=document.querySelector('canvas[aria-label]')||document.querySelector('canvas');"
    L"var out={logueado:!!side,qr:''};if(!side&&qr){try{out.qr=qr.toDataURL('image/png')}catch(e){}}"
    L"out.llamada=!!document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[aria-label=\"Hang up\"],[aria-label=\"Cortar\"]');"
    L"return JSON.stringify(out)})()";

const wchar_t* JS_EN_LLAMADA =
    L"(function(){return !!document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[aria-label=\"Hang up\"],[data-icon=\"call-end\"]')?'si':'no'})()";

// Donde esta el panel flotante de la llamada dentro de la pagina (en
// pixeles fisicos del WebView2 y en CSS px). Vacio si no hay llamada.
const wchar_t* JS_RECT_PANEL =
    L"(function(){var b=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[data-icon=\"call-end\"],[aria-label=\"Hang up\"]');"
    L"if(!b)return '';var c=null,n=b.parentElement;"
    L"while(n&&n!==document.body){var cs=getComputedStyle(n),r=n.getBoundingClientRect();"
    L"if((cs.position==='fixed'||cs.position==='absolute')&&r.width>=200&&r.height>=150&&r.width<innerWidth*0.95){c=n;break}n=n.parentElement;}"
    L"if(!c)return '';var r=c.getBoundingClientRect(),d=window.devicePixelRatio;"
    L"return JSON.stringify({x:r.left*d,y:r.top*d,w:r.width*d,h:r.height*d,cw:r.width,ch:r.height})})()";

constexpr UINT_PTR TIMER_ENCUADRE = 1;
bool g_encuadrando = false;
double g_prop = 900.0 / 620.0;  // proporcion del panel (ancho/alto); la ventana la sigue

// Acomoda el WebView2 adentro de la ventana de video para que se vea solo el
// panel de la llamada: zoom para que el panel llene la ventana (manteniendo
// la proporcion) y desplazamiento para que caiga en (0,0). La pagina no se
// toca: sigue viendo un viewport de ANCHO_PX x ALTO_PX CSS px.
void encuadrar() {
    Vista& v = g_llamada.web ? g_llamada : g_principal;
    if (!g_ventana || !v.web || !v.ctrl || !v.visible || g_encuadrando) return;
    g_encuadrando = true;
    ejecutar(v, JS_RECT_PANEL, [&v](const std::wstring& res) {
        g_encuadrando = false;
        if (!g_ventana || !v.ctrl) return;
        std::string s = resultado_str(res);
        if (s.empty()) return;
        Json j = Json::parsear(s);
        double cw = j["cw"].num(), ch = j["ch"].num();
        if (cw < 10 || ch < 10) return;
        RECT c;
        GetClientRect(g_ventana, &c);
        double Wc = c.right, Hc = c.bottom;
        double dpi = GetDpiForWindow(g_ventana) / 96.0;
        // La ventana toma la proporcion del panel (asi no asoma la pagina alrededor).
        double prop = cw / ch;
        if (std::abs(prop - g_prop) > 0.01 || std::abs(Wc / Hc - prop) > 0.01) {
            g_prop = prop;
            RECT wr;
            GetWindowRect(g_ventana, &wr);
            int extra_w = (wr.right - wr.left) - c.right, extra_h = (wr.bottom - wr.top) - c.bottom;
            int nh = (int)(Wc / prop + 0.5);
            SetWindowPos(g_ventana, nullptr, 0, 0, (int)Wc + extra_w, nh + extra_h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;  // WM_SIZE vuelve a encuadrar
        }
        double z = std::min(Wc / (cw * dpi), Hc / (ch * dpi));
        z = std::clamp(z, 0.25, 5.0);
        double z0 = 1;
        v.ctrl->get_ZoomFactor(&z0);
        if (std::abs(z - z0) > 0.02) {
            // Con el zoom nuevo el panel se mide distinto: se vuelve a medir en el proximo tic.
            v.ctrl->put_ZoomFactor(z);
            int w = (int)(ANCHO_PX * z * dpi), h = (int)(ALTO_PX * z * dpi);
            SetWindowPos(v.hwnd, nullptr, ESCONDIDO, ESCONDIDO, w, h, SWP_NOZORDER);
            return;
        }
        double x = j["x"].num(), y = j["y"].num(), w = j["w"].num(), h = j["h"].num();
        int offx = (int)((Wc - w) / 2), offy = (int)((Hc - h) / 2);
        int pw = (int)(ANCHO_PX * z * dpi), ph = (int)(ALTO_PX * z * dpi);
        SetWindowPos(v.hwnd, nullptr, (int)(offx - x), (int)(offy - y), pw, ph, SWP_NOZORDER | SWP_SHOWWINDOW);
    });
}

void intentar_llamada_pendiente() {
    if (!g_pendiente.hay || !g_principal.web) return;
    g_pendiente.intentos++;
    if (g_pendiente.intentos > 12) {
        registrar("no encontre el boton de llamar; me rindo");
        g_pendiente.hay = false;
        return;
    }
    std::vector<std::wstring> etiquetas = g_pendiente.video ? std::vector<std::wstring>{L"Video call", L"Videollamada", L"video-call"}
                                                            : std::vector<std::wstring>{L"Voice call", L"Llamada de voz", L"Audio call", L"audio-call"};
    ejecutar(g_principal, js_click(etiquetas), [](const std::wstring& r) {
        std::string s = resultado_str(r);
        if (s == "ok") {
            registrar("boton de llamar apretado");
            g_pendiente.hay = false;
            return;
        }
        // Version nueva: un solo boton "Call" que abre un menu.
        if (!g_pendiente.menu_abierto) {
            ejecutar(g_principal, js_click({L"Call", L"Llamar", L"call"}), [](const std::wstring& r2) {
                if (resultado_str(r2) == "ok") g_pendiente.menu_abierto = true;
            });
        }
    });
}

}  // namespace

namespace webwa {

bool iniciar(HWND padre, const std::wstring& carpeta_exe) {
    if (g_activo) return true;
    g_padre = padre;
    if (!g_dll) g_dll = LoadLibraryW((carpeta_exe + L"\\WebView2Loader.dll").c_str());
    if (!g_dll) {
        registrar("falta WebView2Loader.dll");
        return false;
    }
    auto crear = (PFN_CrearEntorno)GetProcAddress(g_dll, "CreateCoreWebView2EnvironmentWithOptions");
    if (!crear) return false;
    std::wstring datos = carpeta_exe + L"\\datos\\webview2";
    CreateDirectoryW((carpeta_exe + L"\\datos").c_str(), nullptr);
    CreateDirectoryW(datos.c_str(), nullptr);
    auto opciones = Make<CoreWebView2EnvironmentOptions>();
    // Que no se duerma por estar fuera de la vista, y etiquetas en ingles
    // (los botones se buscan por su aria-label).
    opciones->put_AdditionalBrowserArguments(
        L"--disable-backgrounding-occluded-windows --disable-renderer-backgrounding --disable-background-timer-throttling "
        L"--autoplay-policy=no-user-gesture-required");
    opciones->put_Language(L"en-US");
    g_activo = true;
    g_cargando = true;
    HRESULT hr = crear(nullptr, datos.c_str(), opciones.Get(),
                       Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                           if (FAILED(hr) || !env) {
                               registrar("no se pudo crear el entorno WebView2 (falta el runtime?)");
                               g_activo = false;
                               g_cargando = false;
                               return S_OK;
                           }
                           g_env = env;
                           crear_vista_principal();
                           return S_OK;
                       }).Get());
    if (FAILED(hr)) {
        registrar("CreateCoreWebView2EnvironmentWithOptions fallo");
        g_activo = false;
        g_cargando = false;
        return false;
    }
    return true;
}

void cerrar() {
    if (!g_activo) return;
    if (g_ventana) DestroyWindow(g_ventana);
    g_ventana = nullptr;
    cerrar_vista(g_llamada);
    cerrar_vista(g_principal);
    g_env.Reset();
    g_activo = false;
    g_logueado = false;
    g_cargando = false;
    g_qr.clear();
    avisar_llamada(false);
}

bool activo() { return g_activo; }
bool logueado() { return g_activo && g_logueado; }
bool cargando() { return g_activo && g_cargando; }
std::string qr_png() { return g_qr; }
bool en_llamada() { return g_en_llamada; }
bool silenciado() { return g_silenciado; }
void al_cambiar_llamada(std::function<void(bool)> f) { g_al_cambiar = std::move(f); }

void sondear() {
    if (!g_activo || !g_principal.web || g_sondeando) return;
    g_sondeando = true;
    ejecutar(g_principal, JS_ESTADO, [](const std::wstring& r) {
        g_sondeando = false;
        std::string s = resultado_str(r);
        if (s.empty()) return;
        Json j = Json::parsear(s);
        bool antes = g_logueado;
        g_logueado = j["logueado"].bul();
        std::string qr = j["qr"].str();
        if (qr.rfind("data:image/png;base64,", 0) == 0) g_qr = base64_decodificar(qr.substr(22));
        else g_qr.clear();
        if (g_logueado != antes) registrar(g_logueado ? "WhatsApp Web logueado" : "WhatsApp Web sin sesion");
        // La llamada puede vivir en la ventanita o en la misma pagina.
        bool en_pagina = j["llamada"].bul();
        if (g_llamada.web) {
            ejecutar(g_llamada, JS_EN_LLAMADA, [en_pagina](const std::wstring& r2) {
                avisar_llamada(resultado_str(r2) == "si" || en_pagina);
            });
        } else {
            avisar_llamada(en_pagina);
        }
        intentar_llamada_pendiente();
    });
}

void llamar(const std::string& telefono, bool video) {
    if (!logueado()) return;
    g_pendiente = {true, video, 0, false};
    registrar("llamar a " + telefono + (video ? " (video)" : ""));
    g_principal.web->Navigate((L"https://web.whatsapp.com/send?phone=" + ancho(telefono)).c_str());
}

void atender() {
    if (!g_activo) return;
    std::wstring js = js_click({L"Accept", L"Aceptar", L"accept-call"});
    if (g_llamada.web) ejecutar(g_llamada, js, [](const std::wstring& r) { registrar("atender (ventanita): " + resultado_str(r)); });
    ejecutar(g_principal, js, [](const std::wstring& r) { registrar("atender (principal): " + resultado_str(r)); });
}

void colgar() {
    if (!g_activo) return;
    std::wstring js = js_click({L"End call", L"Hang up", L"Cortar", L"end-call", L"call-end", L"Decline", L"Rechazar"});
    if (g_llamada.web) ejecutar(g_llamada, js, [](const std::wstring& r) { registrar("colgar (ventanita): " + resultado_str(r)); });
    ejecutar(g_principal, js, [](const std::wstring& r) { registrar("colgar (principal): " + resultado_str(r)); });
    g_pendiente.hay = false;
}

void silenciar(bool si) {
    if (!g_activo) return;
    std::wstring js = js_click(si ? std::vector<std::wstring>{L"Mute", L"Silenciar", L"mic-on"} : std::vector<std::wstring>{L"Unmute", L"Activar micrófono", L"mic-off"});
    Vista& v = g_llamada.web ? g_llamada : g_principal;
    ejecutar(v, js, [si](const std::wstring& r) {
        if (resultado_str(r) == "ok") g_silenciado = si;
        registrar(std::string(si ? "mute: " : "unmute: ") + resultado_str(r));
    });
}

// La ventana propia de la videollamada: adentro va la vista (reparentada).
static LRESULT CALLBACK proc_ventana(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            encuadrar();
            return 0;
        case WM_TIMER:
            // El panel se puede mover o cambiar de tamano: se sigue.
            if (wp == TIMER_ENCUADRE) encuadrar();
            return 0;
        case WM_SIZING: {
            // Se agranda manteniendo la proporcion del area de la llamada.
            RECT* r = (RECT*)lp;
            RECT wr, cr;
            GetWindowRect(h, &wr);
            GetClientRect(h, &cr);
            int extra_w = (wr.right - wr.left) - cr.right, extra_h = (wr.bottom - wr.top) - cr.bottom;
            const double prop = g_prop;
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
        case WM_CLOSE:
            // Cerrar la ventana corta la llamada.
            if (g_al_cerrar_ventana) g_al_cerrar_ventana();
            return 0;
        case WM_DESTROY:
            g_ventana = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void mostrar_llamada(bool si) {
    Vista& v = g_llamada.web ? g_llamada : g_principal;
    if (!v.hwnd) return;
    if (si) {
        if (!g_ventana) {
            static bool registrada = false;
            if (!registrada) {
                WNDCLASSW wc{};
                wc.lpfnWndProc = proc_ventana;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = L"kciwapp2-llamada";
                wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
                wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
                RegisterClassW(&wc);
                registrada = true;
            }
            // Centrada sobre la principal.
            RECT rp;
            GetWindowRect(g_padre, &rp);
            UINT dpi = GetDpiForWindow(g_padre);
            int w = MulDiv(900, dpi, 96), h = MulDiv(620, dpi, 96);
            int x = rp.left + ((rp.right - rp.left) - w) / 2, y = rp.top + ((rp.bottom - rp.top) - h) / 2;
            g_ventana = CreateWindowExW(0, L"kciwapp2-llamada", L"Video call", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
            BOOL oscuro = TRUE;
            DwmSetWindowAttribute(g_ventana, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &oscuro, sizeof oscuro);
        }
        if (GetParent(v.hwnd) != g_ventana) SetParent(v.hwnd, g_ventana);
        // Hasta que se mida el panel, fuera de la vista (fondo negro).
        SetWindowPos(v.hwnd, nullptr, ESCONDIDO, ESCONDIDO, ANCHO_PX, ALTO_PX, SWP_NOZORDER | SWP_SHOWWINDOW);
        ShowWindow(g_ventana, SW_SHOW);
        v.visible = true;
        SetTimer(g_ventana, TIMER_ENCUADRE, 400, nullptr);
        encuadrar();
    } else {
        if (g_ventana) KillTimer(g_ventana, TIMER_ENCUADRE);
        if (GetParent(v.hwnd) != g_padre) SetParent(v.hwnd, g_padre);
        if (v.ctrl) v.ctrl->put_ZoomFactor(1.0);
        SetWindowPos(v.hwnd, nullptr, ESCONDIDO, ESCONDIDO, ANCHO_PX, ALTO_PX, SWP_NOZORDER);
        v.visible = false;
        if (g_ventana) DestroyWindow(g_ventana);
        g_ventana = nullptr;
    }
}

void al_cerrar_ventana(std::function<void()> f) { g_al_cerrar_ventana = std::move(f); }

void volcar_dom() {
    const wchar_t* js =
        L"(function(){var o=[];document.querySelectorAll('[aria-label],[data-icon],[title]').forEach(function(e){"
        L"o.push((e.tagName)+' '+(e.getAttribute('aria-label')||'')+' | '+(e.getAttribute('data-icon')||'')+' | '+(e.getAttribute('title')||''))});"
        L"return location.href+'\\n'+o.join('\\n')})()";
    ejecutar(g_principal, js, [](const std::wstring& r) { registrar("DOM principal:\n" + resultado_str(r)); });
    if (g_llamada.web) ejecutar(g_llamada, js, [](const std::wstring& r) { registrar("DOM llamada:\n" + resultado_str(r)); });
}

}  // namespace webwa
