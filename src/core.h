// El core (kciwapp-core.exe, el server en Go con SQLite) corre al lado del
// cliente cuando la cuenta apunta a 127.0.0.1. Se lanza al arrancar con
// un config generado en datos\ y muere con el cliente (job object).
#pragma once
#include <windows.h>

#include <string>

namespace core {

// Lanza core\kciwapp-core.exe escuchando en 127.0.0.1:puerto con ese token
// (los dos vienen de cuentas.json); base, store y media en carpeta_datos.
// Devuelve false si el exe no esta.
bool iniciar(const std::wstring& carpeta_exe, const std::wstring& carpeta_datos, int puerto, const std::string& token);
// Hay core\kciwapp-core.exe al lado del exe?
bool disponible(const std::wstring& carpeta_exe);
// Un token al azar (24 letras/numeros), para una cuenta nueva.
std::string token_nuevo();
void cerrar();
bool activo();

}  // namespace core
