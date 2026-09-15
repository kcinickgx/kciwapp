// Archivos: dialogos de archivo/carpeta, lectura/escritura de bytes, MIME
// por extension, y todo lo que toca al portapapeles (texto, archivos,
// imagenes).
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace archivos {

// Dialogos del sistema. Devuelven la ruta elegida, o vacio si el usuario
// cancelo.
std::wstring elegir_archivo(HWND duenio);
std::wstring elegir_carpeta(HWND duenio);
std::wstring guardar_como(HWND duenio, const std::wstring& nombre_sugerido);

// Lectura/escritura de bytes crudos. leer_todo devuelve vacio si no pudo
// abrir el archivo (o si el archivo esta vacio: no hay forma de distinguir
// los dos casos por el valor de retorno).
std::string leer_todo(const std::wstring& ruta);
bool escribir_todo(const std::wstring& ruta, const std::string& datos);

// Content-Type por extension; "application/octet-stream" si no la conoce.
std::string mime_de(const std::wstring& ruta);

// Portapapeles.
bool portapapeles_tiene_imagen();
// La imagen del portapapeles codificada como PNG (via WIC): un bitmap
// (CF_DIB/CF_DIBV5), o si no hay, el primer archivo de imagen de un
// CF_HDROP. Vacio si no hay ninguna.
std::string imagen_del_portapapeles_png();
std::vector<std::wstring> archivos_del_portapapeles();  // CF_HDROP
std::wstring texto_del_portapapeles();

// Abrir con el programa asociado / mostrar en el explorador (seleccionado).
void abrir_con_sistema(const std::wstring& ruta);
void mostrar_en_explorador(const std::wstring& ruta);

}  // namespace archivos
