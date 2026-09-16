// Las cuentas del cliente: cuentas.json al lado del exe, una por WhatsApp.
// Cada cuenta es un server (host/puerto/token; 127.0.0.1 = el core local) y
// una carpeta propia bajo datos\ con su cache, media, perfil de WebView2 y,
// si es local, la base del core. Un servidor.json viejo se migra solo.
#pragma once
#include <string>
#include <vector>

struct Cuenta {
    std::wstring nombre;   // el telefono o el nombre de WhatsApp; vacia hasta conectar
    std::string host;
    int puerto = 0;
    std::string token;
    std::wstring carpeta;  // relativa al exe: "datos" o "datos\\cuenta-N"
    bool local() const { return host == "127.0.0.1"; }
};

namespace cuentas {

void cargar(const std::wstring& carpeta_exe);
const std::vector<Cuenta>& lista();
// Agrega y guarda; elige carpeta y, si es local, un puerto libre. Devuelve el indice.
int agregar(Cuenta c);
// El puerto que le toca a la proxima cuenta local (8477 en adelante).
int puerto_local_libre();
void elegir(int i);
int activa();              // -1 si todavia no se eligio
const Cuenta* actual();
// Carpeta de datos de la cuenta activa (absoluta); sin cuenta, <exe>\datos.
std::wstring carpeta_activa();
void poner_nombre(int i, const std::wstring& nombre);
// Lo que se muestra: el nombre, o "This computer" / host:puerto si aun no tiene.
std::wstring etiqueta(const Cuenta& c);
std::wstring detalle(const Cuenta& c);

}  // namespace cuentas
