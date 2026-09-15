// El core (kciwapp-core.exe, el server en Go con SQLite) corre al lado del
// cliente, escuchando en 127.0.0.1. Se lanza al arrancar con un config
// generado en datos\ (token al azar la primera vez) y muere con el cliente
// (job object).
#pragma once
#include <windows.h>

#include <string>

namespace core {

// Lanza el core si existe core\kciwapp-core.exe. Deja en host/puerto/token
// lo que hay que pasarle a red::configurar. Devuelve false si no esta.
bool iniciar(const std::wstring& carpeta_exe, std::wstring& host, int& puerto, std::string& token);
void cerrar();
bool activo();

}  // namespace core
