// La ventana de la llamada, nuestra: foto, nombre, estado (Calling... /
// Connecting... / tiempo), Mute y End. Cuando hay video llegando (y solo
// entonces) la vista de WhatsApp Web se pone encima, ocupando todo.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

namespace vllamada {

void abrir(HINSTANCE inst, const std::wstring& nombre, const std::string& chat, const std::wstring& ruta_foto_http, bool video);
void cerrar();
bool abierta();
// "Calling...", "Connecting...", o vacio (= se muestra el tiempo desde `desde`).
void estado(const std::wstring& texto, unsigned long long desde);
void al_colgar(std::function<void()> f);
// Redibujar (cambio el mute, por ejemplo).
void refrescar();

}  // namespace vllamada
