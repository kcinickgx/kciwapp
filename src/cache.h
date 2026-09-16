// Cache local en SQLite (winsqlite3.dll, cargada a mano con LoadLibrary):
// chats, contactos, mensajes, valores sueltos y que medias ya estan en disco.
// Una sola conexion, protegida con un mutex interno porque se usa tanto
// desde el hilo de la UI como desde hilos de fondo.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "app.h"

namespace cache {

// Abre (o crea) la base en `ruta`; crea las carpetas intermedias si hacen
// falta. WAL + synchronous=NORMAL. Devuelve si pudo abrirla (winsqlite3.dll
// ausente, ruta invalida, etc. dan false).
bool abrir(const std::wstring& ruta);
void cerrar();

// Chats: guardar_chats reemplaza toda la tabla por la lista dada.
void guardar_chats(const std::vector<Chat>& chats);
std::vector<Chat> leer_chats();

// Contactos: guardar_contactos tambien reemplaza toda la tabla.
void guardar_contactos(const std::map<std::string, Contacto>& contactos);
std::map<std::string, Contacto> leer_contactos();

// Mensajes: guardar_mensajes hace INSERT OR REPLACE (upsert), no borra nada.
void guardar_mensajes(const std::vector<Mensaje>& mensajes);
// Los `limite` mensajes de `chat` anteriores a `antes_ts` (0 = sin tope, o
// sea los ultimos `limite`), devueltos en orden cronologico ascendente.
std::vector<Mensaje> leer_mensajes(const std::string& chat, long long antes_ts, int limite);
bool hay_mensaje(const std::string& chat, const std::string& id);
// Tira todos los mensajes guardados (tras una importacion masiva en el server).
void borrar_mensajes();
void marcar_borrado(const std::string& chat, const std::string& id);
// El server cambio el tipo del mensaje (video "foto con musica" -> imagen).
void cambiar_tipo(const std::string& chat, const std::string& id, const std::string& tipo, const std::string& mime);
void editar_texto(const std::string& chat, const std::string& id, const std::wstring& texto);
// El estado solo sube (nunca pisa un estado mas alto con uno mas bajo).
void poner_estado(const std::string& chat, const std::string& id, int estado);
long long ts_mas_viejo(const std::string& chat);   // 0 si no hay mensajes
long long ts_mas_nuevo(const std::string& chat);   // 0 si no hay mensajes

// Busqueda insensible a mayusculas y acentos (columna texto_plano + funcion
// SQL plano()), del mas nuevo al mas viejo.
std::vector<Mensaje> buscar(const std::wstring& texto, int limite);
std::vector<Mensaje> buscar_en(const std::string& chat, const std::wstring& texto, int limite);

// Valores sueltos (cursor de eventos, etc.).
std::string valor(const std::string& clave);
void guardar_valor(const std::string& clave, const std::string& valor);

// Que adjuntos ya estan bajados a disco.
std::wstring ruta_media(long long media_id);  // vacio si no esta anotada
void anotar_media(long long media_id, const std::wstring& ruta, const std::string& mime);

}  // namespace cache
