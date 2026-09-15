// Aviso: notificaciones de escritorio, el globo clasico de la bandeja de
// Windows (Shell_NotifyIcon). El icono es dueno de una ventana oculta,
// "message-only", que vive en su propio hilo con su propio bucle de
// mensajes: ahi es donde llegan los clicks del icono y del globo.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

namespace aviso {

// A que chat/mensaje corresponde un globo (o el ultimo globo mostrado, que
// es al que apunta un click).
struct Destino {
    std::string chat, mensaje;
};

// Arranca el hilo de la bandeja y pone el icono. Se llama una vez; llamadas
// de mas no hacen nada. `icono` tiene que seguir siendo valido mientras la
// app corra (por ejemplo el icono del .exe, cargado con LoadIconW).
void arrancar(HICON icono, const std::wstring& titulo_app);
// Saca el icono de la bandeja y termina el hilo. Bloquea hasta que termina;
// se llama al cerrar la app.
void parar();

// Muestra un globo. Queda anotado que si lo clickean, el destino es
// chat/mensaje_id (ver al_click).
void mostrar(const std::wstring& titulo, const std::wstring& texto, const std::string& chat,
             const std::string& mensaje_id);

// La ventana principal de la app: al hacer click (en el globo, doble click
// en el icono, o "Open" del menu) este modulo la trae al frente con
// SetForegroundWindow + ShowWindow(SW_RESTORE) antes de avisar. Se llama una
// vez, al crear la ventana.
void anotar_principal(HWND h);
// Si la ventana principal esta activa (en primer plano) y no minimizada.
bool esta_al_frente(HWND h);

// Callback para cuando el usuario clickea un globo: se llama desde el hilo
// de la bandeja (no el de la ventana), despues de traer la ventana
// principal al frente. Quien integra tiene que saltar al hilo de la
// ventana (por ejemplo con red::en_ui) antes de tocar el estado de la app.
void al_click(std::function<void(Destino)> f);

}  // namespace aviso
