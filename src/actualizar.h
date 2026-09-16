// Actualizacion del cliente: en https://kcinick.gxzone.com/kciwapp/ estan
// los archivos del cliente y un manifiesto kciwapp.md5 ("md5 tamano ruta"
// por linea, generado por release/publicar.py). Se compara con lo local y
// lo distinto se baja a <archivo>.nuevo; al aplicar, lo viejo pasa a
// <archivo>.viejo (un exe/dll en uso se puede renombrar) y se relanza.
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace actualizar {

struct Archivo {
    std::wstring ruta;  // relativa al exe, con \ (kciwapp2.exe, mpv\libmpv-2.dll)
    std::string md5;
    long long tamano = 0;
};

struct Estado {
    bool verificando = false;
    bool bajando = false;
    bool hay = false;                 // hay archivos distintos al manifiesto
    std::vector<Archivo> pendientes;  // lo que hay que bajar
    long long total_bytes = 0;
    long long bajados = 0;
    int hechos = 0;
    std::wstring actual;  // el archivo que se esta bajando
    std::wstring error;
};

// Baja el manifiesto y compara con los archivos locales, en un hilo; al
// terminar llama a al_terminar en el hilo de la ventana.
void verificar(const std::wstring& carpeta_exe, std::function<void()> al_terminar);
// Baja lo pendiente a *.nuevo, avisando el progreso (hilo de la ventana).
void bajar(const std::wstring& carpeta_exe, std::function<void()> progreso, std::function<void(bool)> al_terminar);
// Renombra: lo actual a *.viejo y *.nuevo a su nombre. Con el core cerrado.
bool aplicar(const std::wstring& carpeta_exe);
// Al arrancar: borra los *.viejo (y *.nuevo sueltos) de la vez anterior.
void limpiar(const std::wstring& carpeta_exe);
Estado estado();

}  // namespace actualizar
