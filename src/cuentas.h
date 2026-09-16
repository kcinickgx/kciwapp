// Las cuentas del cliente: cuentas.json al lado del exe, una por WhatsApp.
// Cada cuenta es un server (host/puerto/token; 127.0.0.1 = el core local) y
// una carpeta propia bajo datos\ con su cache, media, perfil de WebView2 y,
// si es local, la base del core. Un servidor.json viejo se migra solo.
// Solo cuentan las vinculadas: una cuenta sin nombre (nunca conecto) se
// borra al siguiente arranque, con su carpeta.
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

// Lee cuentas.json. Las cuentas que nunca se vincularon (sin nombre) se
// descartan con su carpeta, salvo la del token que se pide conservar (la
// recien agregada, con la que se relanzo el cliente).
void cargar(const std::wstring& carpeta_exe, const std::string& conservar_token = "");
// Indice de la cuenta con ese token, o -1.
int indice_de(const std::string& token);
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
