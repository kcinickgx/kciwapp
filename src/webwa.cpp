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

constexpr int ANCHO_PX = 1920, ALTO_PX = 1200;
constexpr int ESCONDIDO = -20000;

HMODULE g_dll = nullptr;
HWND g_padre = nullptr;
ComPtr<ICoreWebView2Environment> g_env;
Vista g_principal, g_llamada;
bool g_activo = false, g_logueado = false, g_cargando = true, g_en_llamada = false, g_silenciado = false;
std::string g_qr;
std::function<void(bool)> g_al_cambiar;
std::function<void()> g_al_conectar;
double g_prop = 900.0 / 620.0;  // proporcion del panel (ancho/alto); la ventana la sigue
bool g_video_fluye = false;  // llega video del otro lado
bool g_conectada = false;    // atendida (el panel muestra el reloj)
int g_popout_intentos = 0;      // clicks al boton de "abrir en ventana" de la llamada
bool g_quiero_popout = false;   // solo en las de video
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
    if (!en) {
        g_silenciado = false;
        g_video_fluye = false;
        g_conectada = false;
    }
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
            // El boton "Return to WhatsApp" de la ventanita no tiene sentido
            // aca (no hay a donde volver): se esconde apenas aparezca.
            g_llamada.web->AddScriptToExecuteOnDocumentCreated(
                L"(function(){function tapar(){"
                // Por etiqueta o icono (return / compartir pantalla).
                L"document.querySelectorAll('button,[role=button],a').forEach(function(b){"
                L"var t=((b.getAttribute('aria-label')||'')+' '+(b.getAttribute('title')||'')+' '+(b.innerText||'')).toLowerCase();"
                L"var ic=b.querySelector('[data-icon]');var di=(b.getAttribute('data-icon')||(ic?ic.getAttribute('data-icon'):'')||'').toLowerCase();"
                L"if(t.indexOf('return to whatsapp')>=0||t.indexOf('back to whatsapp')>=0||t.indexOf('share screen')>=0||t.indexOf('screen share')>=0||"
                L"di.indexOf('share')>=0||di.indexOf('screen')>=0||di.indexOf('return')>=0)b.style.display='none'});"
                // Por posicion: el que esta justo a la izquierda del de cortar es el de volver.
                L"var e=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[data-icon=\"call-end\"],[aria-label=\"Hang up\"]');"
                L"if(e){var er=e.getBoundingClientRect();var fila=e;for(var i=0;i<4&&fila.parentElement;i++)fila=fila.parentElement;"
                L"var bs=Array.prototype.filter.call(fila.querySelectorAll('button,[role=button]'),function(x){var r=x.getBoundingClientRect();"
                L"return r.width>0&&x!==e&&!x.contains(e)&&!e.contains(x)&&Math.abs((r.top+r.height/2)-(er.top+er.height/2))<er.height&&r.right<=er.left+2});"
                L"if(bs.length){bs.sort(function(a,c){return a.getBoundingClientRect().left-c.getBoundingClientRect().left});"
                L"var u=bs[bs.length-1];if(er.left-u.getBoundingClientRect().right<40)u.style.display='none';}}}"
                L"new MutationObserver(tapar).observe(document.documentElement,{childList:true,subtree:true});setInterval(tapar,500);tapar()})()",
                nullptr);
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
                    ComPtr<ICoreWebView2WindowFeatures> wf;
                    if (SUCCEEDED(args->get_WindowFeatures(&wf)) && wf) {
                        UINT w = 0, h = 0;
                        BOOL hw = FALSE, hh = FALSE;
                        wf->get_HasSize(&hw);
                        if (hw) {
                            wf->get_Width(&w);
                            wf->get_Height(&h);
                            if (w > 100 && h > 100) g_prop = (double)w / h;
                        }
                        (void)hh;
                    }
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
    L"var ec=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[aria-label=\"Hang up\"],[aria-label=\"Cortar\"]');out.llamada=!!ec;out.conectada=false;"
    L"if(ec){var c=ec;for(var i=0;i<8&&c.parentElement&&c.parentElement!==document.body;i++)c=c.parentElement;var t=c.innerText||'';"
    L"out.conectada=!/calling|ringing|connecting|llamando|conectando/i.test(t)&&/(^|\\s)\\d{1,2}:\\d\\d(\\s|$)/.test(t)}"
    L"return JSON.stringify(out)})()";

// Hay un video del otro lado andando (el nuestro esta muted; el remoto no).
const wchar_t* JS_VIDEO =
    L"(function(){var vs=Array.prototype.filter.call(document.querySelectorAll('video'),function(v){return v.videoWidth>0&&v.readyState>=2&&!v.paused});"
    L"if(!vs.length)return 'no';"
    L"function remoto(v){try{var t=v.srcObject&&v.srcObject.getVideoTracks();return t&&t.length&&!t[0].label}catch(e){return false}}"
    L"if(vs.some(remoto))return 'si';"
    L"if(vs.length>=2)return 'si';"
    L"var r=vs[0].getBoundingClientRect();return (r.width>=innerWidth*0.5)?'si':'no'})()";

const wchar_t* JS_DIAG_VIDEO =
    L"(function(){try{var o=['url='+location.href+' title='+document.title+' readyState='+document.readyState+' vis='+document.visibilityState+' inner='+innerWidth+'x'+innerHeight+' outer='+outerWidth+'x'+outerHeight+' body='+(document.body?document.body.children.length:-1)];"
    L"var vs=document.querySelectorAll('video');o.push('videos='+vs.length+' iframes='+document.querySelectorAll('iframe').length+' canvas='+document.querySelectorAll('canvas').length);"
    L"for(var i=0;i<vs.length;i++){var v=vs[i];var r=v.getBoundingClientRect();var lab='';"
    L"try{var t=v.srcObject&&v.srcObject.getVideoTracks();lab=t&&t.length?('['+t[0].label+']'):'sin-track'}catch(e){lab='err'}"
    L"o.push('video '+v.videoWidth+'x'+v.videoHeight+' rs='+v.readyState+' paused='+v.paused+' muted='+v.muted+' rect='+Math.round(r.width)+'x'+Math.round(r.height)+' '+lab)}"
    L"var e=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[data-icon=\"call-end\"],[aria-label=\"Hang up\"]');"
    L"o.push('cortar='+(e?'si':'no'));var t2=(document.body?document.body.innerText:'')||'';o.push('texto: '+t2.replace(/\s+/g,' ').slice(0,300));"
    L"return o.join(' || ')}catch(e){return 'ERR '+e}})()";

const wchar_t* JS_BOTONES =
    L"(function(){var o=[];document.querySelectorAll('button,[role=button]').forEach(function(b){var r=b.getBoundingClientRect();if(r.width<=0)return;"
    L"var ic=b.querySelector('[data-icon]');o.push((b.getAttribute('aria-label')||'')+'|'+(b.getAttribute('title')||'')+'|'+(ic?ic.getAttribute('data-icon'):(b.getAttribute('data-icon')||''))+'|'+(b.innerText||'').trim().slice(0,30))});"
    L"return o.join(' ; ')})()";

const wchar_t* JS_EN_LLAMADA =
    L"(function(){var e=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[data-icon=\"call-end\"],[aria-label=\"Hang up\"]');"
    L"if(!e)return 'no';var c=e;for(var i=0;i<8&&c.parentElement&&c.parentElement!==document.body;i++)c=c.parentElement;"
    L"var t=(c.innerText||'');if(/calling|ringing|connecting|llamando|conectando/i.test(t))return 'si';"
    L"return /(^|\s)\d{1,2}:\d\d(\s|$)/.test(t)?'conectada':'si'})()";

const wchar_t* JS_POPOUT =
    L"(function(){var b=document.querySelector('[aria-label=\"Move to new window\"],[title=\"Move to new window\"],[aria-label=\"Pop out\"],[aria-label=\"Pop-out\"],[aria-label=\"Open in new window\"],[aria-label=\"Expand\"],"
    L"[aria-label=\"Picture in picture\"],[data-icon=\"pop-out\"],[data-icon=\"popout\"],[data-icon=\"expand\"],[data-icon=\"pip\"],[data-icon=\"new-window\"]');"
    L"if(b){b.click();return 'ok etiqueta'}"
    L"var e=document.querySelector('[aria-label=\"End call\"],[data-icon=\"end-call\"],[data-icon=\"call-end\"],[aria-label=\"Hang up\"]');"
    L"if(!e)return 'sin cortar';var fila=e;for(var i=0;i<4;i++){fila=fila.parentElement;if(!fila)return 'sin fila';"
    L"var bs=Array.prototype.filter.call(fila.querySelectorAll('button,[role=button]'),function(x){var r=x.getBoundingClientRect();return r.width>0&&!e.contains(x)&&x!==e&&!x.contains(e)});"
    L"var er=e.getBoundingClientRect();bs=bs.filter(function(x){var r=x.getBoundingClientRect();return Math.abs((r.top+r.height/2)-(er.top+er.height/2))<er.height});"
    L"if(bs.length>=2){bs.sort(function(a,c){return a.getBoundingClientRect().left-c.getBoundingClientRect().left});"
    L"var t=bs[bs.length-2];t.click();return 'ok fila '+(t.getAttribute('aria-label')||t.getAttribute('title')||t.outerHTML.slice(0,80))}}return 'no'})()";

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
        L"--disable-features=CalculateNativeWinOcclusion --autoplay-policy=no-user-gesture-required");
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
    poner_vista(nullptr, nullptr);
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
void al_conectar_llamada(std::function<void()> f) { g_al_conectar = std::move(f); }

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
        if (en_pagina && j["conectada"].bul() && !g_conectada && !g_llamada.web) {
            g_conectada = true;
            registrar("llamada conectada (pagina)");
            if (g_al_conectar) g_al_conectar();
        }
        if (g_llamada.web) {
            ejecutar(g_llamada, JS_EN_LLAMADA, [en_pagina](const std::wstring& r2) {
                std::string e = resultado_str(r2);
                avisar_llamada(e == "si" || e == "conectada" || en_pagina);
                if (e == "conectada" && !g_conectada) {
                    g_conectada = true;
                    registrar("llamada conectada");
                    if (g_al_conectar) g_al_conectar();
                }
            });
            ejecutar(g_llamada, JS_VIDEO, [](const std::wstring& r2) {
                g_video_fluye = resultado_str(r2) == "si";
                static int cada = 0;
                if (!g_video_fluye && (cada++ % 5) == 0)
                    ejecutar(g_llamada, JS_DIAG_VIDEO, [](const std::wstring& r3) { registrar("diag ventanita crudo: " + angosto(r3).substr(0, 600)); });
            });
        } else {
            avisar_llamada(en_pagina);
            ejecutar(g_principal, JS_VIDEO, [](const std::wstring& r2) { g_video_fluye = resultado_str(r2) == "si"; });
        }
        intentar_llamada_pendiente();
        if (g_en_llamada && g_quiero_popout && !g_llamada.web && !g_llamada.creando && g_popout_intentos < 6) {
            g_popout_intentos++;
            ejecutar(g_principal, JS_POPOUT, [](const std::wstring& r) { registrar("pop-out: " + resultado_str(r)); });
        }
    });
}

void llamar(const std::string& telefono, bool video) {
    if (!logueado()) return;
    g_pendiente = {true, video, 0, false};
    g_quiero_popout = video;
    g_popout_intentos = 0;
    registrar("llamar a " + telefono + (video ? " (video)" : ""));
    g_principal.web->Navigate((L"https://web.whatsapp.com/send?phone=" + ancho(telefono)).c_str());
}

void querer_popout(bool si) {
    g_quiero_popout = si;
    g_popout_intentos = 0;
}

void atender() {
    if (!g_activo) return;
    std::wstring js = js_click({L"Accept", L"Aceptar", L"Answer", L"accept-call", L"call-accept", L"answer", L"Accept call", L"Answer call"});
    if (g_llamada.web) ejecutar(g_llamada, js, [](const std::wstring& r) { registrar("atender (ventanita): " + resultado_str(r)); });
    ejecutar(g_principal, js, [](const std::wstring& r) {
        std::string res = resultado_str(r);
        registrar("atender (principal): " + res);
        if (res != "ok") ejecutar(g_principal, JS_BOTONES, [](const std::wstring& r2) { registrar("botones principal: " + resultado_str(r2)); });
    });
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

// La vista de la llamada, como hija de la ventana que la muestra (o escondida).
void poner_vista(HWND padre, const RECT* r) {
    Vista& v = g_llamada.web ? g_llamada : g_principal;
    if (!v.hwnd) return;
    if (padre && r) {
        if (GetParent(v.hwnd) != padre) SetParent(v.hwnd, padre);
        SetWindowPos(v.hwnd, HWND_TOP, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_SHOWWINDOW);
        v.visible = true;
    } else {
        for (Vista* x : {&g_llamada, &g_principal}) {
            if (!x->hwnd) continue;
            if (GetParent(x->hwnd) != g_padre) SetParent(x->hwnd, g_padre);
            SetWindowPos(x->hwnd, nullptr, ESCONDIDO, ESCONDIDO, ANCHO_PX, ALTO_PX, SWP_NOZORDER);
            x->visible = false;
        }
    }
    if (v.ctrl) {
        RECT c;
        GetClientRect(v.hwnd, &c);
        v.ctrl->put_Bounds(c);
    }
}

bool video_fluye() { return g_en_llamada && g_video_fluye; }
double proporcion_video() { return g_prop; }

void volcar_dom() {
    const wchar_t* js =
        L"(function(){var o=[];document.querySelectorAll('[aria-label],[data-icon],[title]').forEach(function(e){"
        L"o.push((e.tagName)+' '+(e.getAttribute('aria-label')||'')+' | '+(e.getAttribute('data-icon')||'')+' | '+(e.getAttribute('title')||''))});"
        L"return location.href+'\\n'+o.join('\\n')})()";
    ejecutar(g_principal, js, [](const std::wstring& r) { registrar("DOM principal:\n" + resultado_str(r)); });
    if (g_llamada.web) ejecutar(g_llamada, js, [](const std::wstring& r) { registrar("DOM llamada:\n" + resultado_str(r)); });
}

}  // namespace webwa
