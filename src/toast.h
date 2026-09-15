// Avisos propios: una ventanita siempre arriba (sin robar el foco) con el
// nombre, el texto y la foto del chat, dibujada con nuestro Gfx y los
// colores del tema. En el monitor y la esquina que diga settings.
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace toast {

// Se llama una vez, en el hilo de la ventana principal (el aviso vive en el
// mismo loop de mensajes). `al_click` recibe el chat y el mensaje del aviso.
void iniciar(HINSTANCE inst, std::function<void(const std::string& chat, const std::string& mensaje)> al_click);
void mostrar(const std::wstring& titulo, const std::wstring& texto, const std::string& chat,
             const std::string& mensaje, const std::wstring& ruta_foto_http);
// Nombres de los monitores conectados (indice = el que se guarda en settings).
std::vector<std::wstring> monitores();
void cerrar_todos();

}  // namespace toast
