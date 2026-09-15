// WhatsApp Web adentro de un WebView2 escondido, solo para llamadas (whatsmeow
// no tiene audio/video). Corre como otro "dispositivo vinculado"; se vincula
// una vez con el QR que sacamos del DOM y dibujamos nosotros. Las llamadas de
// voz van totalmente ocultas; para las de video se muestra la ventana de la
// llamada de WhatsApp Web adentro de la nuestra, sobre la conversacion.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

namespace webwa {

// Arranca (carga WebView2Loader.dll de la carpeta del exe y crea el perfil en
// <datos>\webview2). `padre` es la ventana principal: los WebView2 son hijos
// suyos, fuera del area visible. Devuelve false si no hay runtime.
bool iniciar(HWND padre, const std::wstring& carpeta_exe);
void cerrar();
bool activo();

// Estado de la sesion de WhatsApp Web.
bool logueado();
bool cargando();
// PNG del QR para vincular (vacio si no hay QR en pantalla ahora).
std::string qr_png();
// Se llama cada 2 s desde el timer de la ventana principal.
void sondear();

// Llamar a un numero (jid sin @...). Navega al chat y aprieta el boton.
void llamar(const std::string& telefono, const std::wstring& nombre, bool video);
// Atender la llamada entrante que WhatsApp Web esta mostrando.
void atender();
// Que la llamada en curso (video) pase a la ventanita aparte de WhatsApp Web.
void querer_popout(bool si);
void colgar();
void silenciar(bool si);
bool en_llamada();
bool silenciado();
// Pone la vista de la llamada (la ventanita de WhatsApp Web, o la principal
// si no hay) como hija de `padre` en el rectangulo `r` (pixeles fisicos), o la
// devuelve escondida a la ventana principal si padre es nulo.
void poner_vista(HWND padre, const RECT* r);
// Si esta llegando video del otro lado (recien ahi vale la pena mostrar la web).
bool video_fluye();
// Proporcion ancho/alto que pidio la ventanita de WhatsApp Web (para el resize).
double proporcion_video();

// Avisos hacia la app (en el hilo de la UI).
void al_cambiar_llamada(std::function<void(bool en_llamada)> f);
// La atendieron (el panel de WhatsApp Web muestra el reloj): arranca el contador.
void al_conectar_llamada(std::function<void()> f);

// Debug: vuelca los aria-label / data-icon que hay en las dos paginas al log.
void volcar_dom();

}  // namespace webwa
