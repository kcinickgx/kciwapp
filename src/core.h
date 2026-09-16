// El core (kciwapp-core.exe, el server en Go con SQLite) corre al lado del
// cliente cuando servidor.json apunta a 127.0.0.1. Se lanza al arrancar con
// un config generado en datos\ y muere con el cliente (job object).
#pragma once
#include <windows.h>

#include <string>

namespace core {

// Lanza core\kciwapp-core.exe escuchando en 127.0.0.1:puerto con ese token
// (los dos vienen de servidor.json). Devuelve false si el exe no esta.
bool iniciar(const std::wstring& carpeta_exe, int puerto, const std::string& token);
// Hay core\kciwapp-core.exe al lado del exe?
bool disponible(const std::wstring& carpeta_exe);
// Un token al azar (24 letras/numeros), para servidor.json la primera vez.
std::string token_nuevo();
void cerrar();
bool activo();

}  // namespace core
