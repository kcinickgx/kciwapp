// Reproduccion de audio y video con libmpv, cargada dinamicamente (sin
// vincular contra mpv.lib): LoadLibraryW + GetProcAddress de las funciones
// de la API de cliente de mpv. La DLL vive en <carpeta del exe>\mpv\ (ver
// portable\mpv\LEEME.txt). Un solo archivo a la vez por instancia.
#pragma once
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// mpv_handle es un struct opaco de libmpv; no hace falta el header de mpv
// aca, solo en reproductor.cpp.
struct mpv_handle;

struct Reproductor {
    Reproductor();
    ~Reproductor();
    Reproductor(const Reproductor&) = delete;
    Reproductor& operator=(const Reproductor&) = delete;

    // Carga <carpeta_exe>\mpv\libmpv-2.dll y arranca mpv. Devuelve false si
    // la DLL no esta ahi o alguna funcion necesaria no se pudo resolver.
    bool iniciar(const std::wstring& carpeta_exe);

    // Abre un archivo y lo empieza a reproducir. Si ventana_video no es
    // null, mpv dibuja el video ahi (opcion "wid", la ventana debe ser hija
    // de la ventana principal); si es null, solo se reproduce el audio
    // (opcion "vid=no").
    void abrir(const std::wstring& ruta_archivo, HWND ventana_video = nullptr);

    void reproducir();
    void pausar();
    bool pausado() const;
    void ir_a(double segundos);
    double posicion() const;
    double duracion() const;
    // true cuando el archivo actual llego al final (eof-reached). No se
    // pone en true al llamar a parar().
    bool terminado() const;
    void parar();

    // Dispositivo de salida de audio: pasar el id tal cual lo devuelve
    // grabador::dispositivos(false) (mismo formato de id que usa WASAPI), o
    // vacio para "auto" (el dispositivo por defecto del sistema).
    void salida(const std::wstring& id_dispositivo_wasapi_o_vacio);

    // Se llama desde el hilo interno de eventos de mpv cada vez que cambia
    // time-pos/duration/pause, o cuando el archivo termina. OJO: NO es el
    // hilo de la ventana; quien integre esto tiene que pasar al hilo de UI
    // (por ejemplo con red::en_ui) antes de tocar Gfx o la UI.
    std::function<void()> al_cambiar;

   private:
    HMODULE biblioteca = nullptr;
    mpv_handle* mpv = nullptr;

    // Punteros resueltos con GetProcAddress; se castean al tipo real (ver
    // los PFN_* en reproductor.cpp) justo antes de cada llamada, asi este
    // header no necesita incluir mpv/client.h.
    void* fn_create = nullptr;
    void* fn_initialize = nullptr;
    void* fn_set_option_string = nullptr;
    void* fn_command = nullptr;
    void* fn_command_string = nullptr;
    void* fn_set_property_string = nullptr;
    void* fn_get_property = nullptr;
    void* fn_observe_property = nullptr;
    void* fn_wait_event = nullptr;
    void* fn_set_wakeup_callback = nullptr;
    void* fn_terminate_destroy = nullptr;
    void* fn_free = nullptr;
    void* fn_client_api_version = nullptr;

    std::atomic<double> pos_{0.0};
    std::atomic<double> dur_{0.0};
    std::atomic<bool> pausado_{true};
    std::atomic<bool> eof_{false};

    // Hilo que bombea mpv_wait_event(); se despierta con el wakeup callback
    // de mpv en vez de bloquear con timeout infinito, como recomienda
    // client.h.
    std::thread hilo_eventos;
    std::mutex mu_despertar;
    std::condition_variable cv_despertar;
    bool despertar_pendiente = false;
    bool salir_hilo = false;

    void bucle_eventos();
    static void despertar_trampolin(void* datos);
    // Devuelve true si el evento fue MPV_EVENT_SHUTDOWN (en ese caso ya se
    // hizo mpv_terminate_destroy y "mpv" quedo en null: no tocarlo mas).
    bool procesar_evento(void* evento);
};
