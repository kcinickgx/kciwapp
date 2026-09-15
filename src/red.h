// Red: HTTP contra el server (WinHTTP, sincronico, para usar desde hilos de
// fondo) y el pasamanos de trabajos entre hilos y el hilo de la ventana.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include "json.h"

struct Respuesta {
    int estado = 0;  // 0 = no se pudo conectar
    std::string cuerpo;
    std::string tipo;  // Content-Type
    bool ok() const { return estado >= 200 && estado < 300; }
};

namespace red {

// Direccion y token del server; se leen de %APPDATA%\kciwapp2\servidor.json.
void configurar(const std::wstring& host, int puerto, const std::string& token);
bool configurado();

Respuesta pedir(const wchar_t* metodo, const std::wstring& ruta, const std::string& cuerpo = "",
                const wchar_t* tipo = L"application/json", int espera_ms = 30000);
Respuesta obtener(const std::wstring& ruta, int espera_ms = 30000);
Respuesta mandar_json(const std::wstring& ruta, const std::string& json);
// Multipart con un archivo, para /enviar.
Respuesta mandar_archivo(const std::wstring& ruta, const std::string& chat, const std::string& nombre,
                         const std::string& mime, const std::string& datos, const std::string& texto,
                         const std::string& cita, const std::string& tipo, int segundos);

// Trabajo en un hilo de fondo.
void en_fondo(std::function<void()> f);
// Trabajo en el hilo de la ventana (se encola con PostMessage).
void en_ui(std::function<void()> f);
// La ventana que recibe los trabajos; se llama una vez al crearla.
void anotar_ventana(HWND h);
// Mensaje que usa en_ui; la ventana lo atiende llamando a esto.
const UINT WM_TRABAJO = WM_APP + 7;
void atender_trabajo(LPARAM lp);
// Una linea al log de depuracion (portable/debug.log).
void registrar(const std::string& linea);

}  // namespace red

// UTF-8 <-> UTF-16.
std::wstring ancho(const std::string& s);
std::string angosto(const std::wstring& s);
