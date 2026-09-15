#include "reproductor.h"

#include "mpv/client.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Tipos de las funciones de la API de cliente de mpv que usamos, para
// castear los void* que devuelve GetProcAddress antes de llamarlos.
using PFN_create = mpv_handle* (*)();
using PFN_initialize = int (*)(mpv_handle*);
using PFN_set_option_string = int (*)(mpv_handle*, const char*, const char*);
using PFN_command = int (*)(mpv_handle*, const char**);
using PFN_command_string = int (*)(mpv_handle*, const char*);
using PFN_set_property_string = int (*)(mpv_handle*, const char*, const char*);
using PFN_get_property = int (*)(mpv_handle*, const char*, mpv_format, void*);
using PFN_observe_property = int (*)(mpv_handle*, uint64_t, const char*, mpv_format);
using PFN_wait_event = mpv_event* (*)(mpv_handle*, double);
using PFN_set_wakeup_callback = void (*)(mpv_handle*, void (*)(void*), void*);
using PFN_terminate_destroy = void (*)(mpv_handle*);
using PFN_free = void (*)(void*);
using PFN_client_api_version = unsigned long (*)();

// UTF-8 <-> UTF-16 propio del modulo: para no depender de red.cpp (este
// archivo tiene que poder compilarse solo, como en build/test/treprod.cpp).
std::string a_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

}  // namespace

Reproductor::Reproductor() = default;

Reproductor::~Reproductor() {
    if (mpv) {
        // Le pedimos a mpv que cierre. El hilo de eventos va a recibir
        // MPV_EVENT_SHUTDOWN, va a hacer el mpv_terminate_destroy() el
        // mismo (es el unico hilo al que le queda permitido tocar el
        // contexto en ese momento) y va a terminar solo.
        reinterpret_cast<PFN_command_string>(fn_command_string)(mpv, "quit");
    } else {
        std::lock_guard<std::mutex> lk(mu_despertar);
        salir_hilo = true;
    }
    cv_despertar.notify_all();
    if (hilo_eventos.joinable()) hilo_eventos.join();
    if (biblioteca) {
        FreeLibrary(biblioteca);
        biblioteca = nullptr;
    }
}

bool Reproductor::iniciar(const std::wstring& carpeta_exe) {
    std::wstring carpeta_mpv = carpeta_exe;
    if (!carpeta_mpv.empty() && carpeta_mpv.back() != L'\\') carpeta_mpv += L'\\';
    carpeta_mpv += L"mpv";

    // Para que libmpv-2.dll encuentre sus propias dependencias (ffmpeg,
    // libass, etc.) que viven al lado en la misma carpeta.
    SetDllDirectoryW(carpeta_mpv.c_str());
    std::wstring ruta_dll = carpeta_mpv + L"\\libmpv-2.dll";
    HMODULE h = LoadLibraryExW(ruta_dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) return false;
    biblioteca = h;

    auto cargar = [h](const char* nombre) -> void* { return (void*)GetProcAddress(h, nombre); };
    fn_create = cargar("mpv_create");
    fn_initialize = cargar("mpv_initialize");
    fn_set_option_string = cargar("mpv_set_option_string");
    fn_command = cargar("mpv_command");
    fn_command_string = cargar("mpv_command_string");
    fn_set_property_string = cargar("mpv_set_property_string");
    fn_get_property = cargar("mpv_get_property");
    fn_observe_property = cargar("mpv_observe_property");
    fn_wait_event = cargar("mpv_wait_event");
    fn_set_wakeup_callback = cargar("mpv_set_wakeup_callback");
    fn_terminate_destroy = cargar("mpv_terminate_destroy");
    fn_free = cargar("mpv_free");
    fn_client_api_version = cargar("mpv_client_api_version");

    if (!fn_create || !fn_initialize || !fn_set_option_string || !fn_command || !fn_command_string ||
        !fn_set_property_string || !fn_get_property || !fn_observe_property || !fn_wait_event ||
        !fn_set_wakeup_callback || !fn_terminate_destroy || !fn_free || !fn_client_api_version) {
        FreeLibrary(h);
        biblioteca = nullptr;
        return false;
    }

    mpv = reinterpret_cast<PFN_create>(fn_create)();
    if (!mpv) return false;

    auto opt = reinterpret_cast<PFN_set_option_string>(fn_set_option_string);
    opt(mpv, "keep-open", "yes");
    opt(mpv, "idle", "yes");
    opt(mpv, "input-default-bindings", "no");
    opt(mpv, "osc", "no");
    opt(mpv, "osd-bar", "no");     // sin la barra de mpv al seekear: la dibujamos nosotros
    opt(mpv, "osd-level", "0");
    opt(mpv, "terminal", "no");

    // Hay que registrar el wakeup callback antes de mpv_initialize(): mpv
    // puede generar eventos desde ese mismo llamado.
    reinterpret_cast<PFN_set_wakeup_callback>(fn_set_wakeup_callback)(mpv, &Reproductor::despertar_trampolin, this);

    int r = reinterpret_cast<PFN_initialize>(fn_initialize)(mpv);
    if (r < 0) {
        reinterpret_cast<PFN_terminate_destroy>(fn_terminate_destroy)(mpv);
        mpv = nullptr;
        return false;
    }

    auto obs = reinterpret_cast<PFN_observe_property>(fn_observe_property);
    obs(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    obs(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    obs(mpv, 0, "pause", MPV_FORMAT_FLAG);
    obs(mpv, 0, "eof-reached", MPV_FORMAT_FLAG);

    salir_hilo = false;
    hilo_eventos = std::thread(&Reproductor::bucle_eventos, this);
    return true;
}

void Reproductor::abrir(const std::wstring& ruta_archivo, HWND ventana_video) {
    if (!mpv) return;
    pos_.store(0.0);
    dur_.store(0.0);
    eof_.store(false);

    auto set_opt = reinterpret_cast<PFN_set_option_string>(fn_set_option_string);
    auto set_prop = reinterpret_cast<PFN_set_property_string>(fn_set_property_string);
    if (ventana_video) {
        set_prop(mpv, "vid", "auto");
        char wid[32];
        snprintf(wid, sizeof(wid), "%lld", (long long)(intptr_t)ventana_video);
        set_opt(mpv, "wid", wid);
    } else {
        set_prop(mpv, "vid", "no");
    }

    std::string ruta_u8 = a_utf8(ruta_archivo);
    const char* args[] = {"loadfile", ruta_u8.c_str(), nullptr};
    reinterpret_cast<PFN_command>(fn_command)(mpv, args);
}

void Reproductor::reproducir() {
    if (!mpv) return;
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "pause", "no");
}

void Reproductor::pausar() {
    if (!mpv) return;
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "pause", "yes");
}

bool Reproductor::pausado() const { return pausado_.load(); }

void Reproductor::ir_a(double segundos) {
    if (!mpv) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "seek %.3f absolute", segundos);
    reinterpret_cast<PFN_command_string>(fn_command_string)(mpv, cmd);
}

double Reproductor::posicion() const { return pos_.load(); }
double Reproductor::duracion() const { return dur_.load(); }
bool Reproductor::terminado() const { return eof_.load(); }

void Reproductor::parar() {
    if (!mpv) return;
    reinterpret_cast<PFN_command_string>(fn_command_string)(mpv, "stop");
    pos_.store(0.0);
    eof_.store(false);
}

void Reproductor::salida(const std::wstring& id_dispositivo_wasapi_o_vacio) {
    if (!mpv) return;
    std::string valor = id_dispositivo_wasapi_o_vacio.empty() ? "auto" : "wasapi/" + a_utf8(id_dispositivo_wasapi_o_vacio);
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "audio-device", valor.c_str());
}

void Reproductor::volumen(int v) {
    if (!mpv) return;
    char buf[32];
    snprintf(buf, sizeof buf, "%d", std::clamp(v, 0, 100));
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "volume", buf);
}

void Reproductor::silencio(bool si) {
    if (!mpv) return;
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "mute", si ? "yes" : "no");
}

void Reproductor::velocidad(double v) {
    if (!mpv) return;
    char buf[32];
    snprintf(buf, sizeof buf, "%.2f", v);
    reinterpret_cast<PFN_set_property_string>(fn_set_property_string)(mpv, "speed", buf);
}

void Reproductor::despertar_trampolin(void* datos) {
    auto* self = static_cast<Reproductor*>(datos);
    {
        std::lock_guard<std::mutex> lk(self->mu_despertar);
        self->despertar_pendiente = true;
    }
    // El wakeup callback de mpv puede llegar desde cualquier hilo interno
    // de mpv y tiene que ser rapidisimo: nada de llamadas a mpv_* aca
    // adentro, solo despertar a nuestro hilo de eventos.
    self->cv_despertar.notify_one();
}

bool Reproductor::procesar_evento(void* evento) {
    auto* ev = static_cast<mpv_event*>(evento);
    bool cambio = false;

    if (ev->event_id == MPV_EVENT_SHUTDOWN) {
        reinterpret_cast<PFN_terminate_destroy>(fn_terminate_destroy)(mpv);
        mpv = nullptr;
        return true;
    }

    if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
        auto* p = static_cast<mpv_event_property*>(ev->data);
        if (p && p->data) {
            if (!strcmp(p->name, "time-pos") && p->format == MPV_FORMAT_DOUBLE) {
                pos_.store(*static_cast<double*>(p->data));
                cambio = true;
            } else if (!strcmp(p->name, "duration") && p->format == MPV_FORMAT_DOUBLE) {
                dur_.store(*static_cast<double*>(p->data));
                cambio = true;
            } else if (!strcmp(p->name, "pause") && p->format == MPV_FORMAT_FLAG) {
                pausado_.store(*static_cast<int*>(p->data) != 0);
                cambio = true;
            } else if (!strcmp(p->name, "eof-reached") && p->format == MPV_FORMAT_FLAG) {
                eof_.store(*static_cast<int*>(p->data) != 0);
                cambio = true;
            }
        }
    } else if (ev->event_id == MPV_EVENT_END_FILE) {
        cambio = true;
    }

    if (cambio && al_cambiar) al_cambiar();
    return false;
}

void Reproductor::bucle_eventos() {
    auto wait_event = reinterpret_cast<PFN_wait_event>(fn_wait_event);
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(mu_despertar);
            cv_despertar.wait(lk, [this] { return despertar_pendiente || salir_hilo; });
            despertar_pendiente = false;
            if (salir_hilo) return;
        }
        for (;;) {
            mpv_event* ev = wait_event(mpv, 0.0);
            if (!ev || ev->event_id == MPV_EVENT_NONE) break;
            if (procesar_evento(ev)) return;  // shutdown: mpv ya quedo en null
        }
    }
}
