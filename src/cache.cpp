// Implementacion de cache.h. Carga winsqlite3.dll a mano (no hay sqlite3.h
// ni sqlite3.c en el proyecto: viene con Windows) y declara aca lo minimo de
// su API que hace falta.
#include "cache.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "json.h"
#include "red.h"

// ---- API minima de winsqlite3.dll, cargada con GetProcAddress -------------

struct sqlite3;
struct sqlite3_stmt;
struct sqlite3_value;
struct sqlite3_context;
using sqlite3_int64 = long long;

namespace {

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;
constexpr int SQLITE_OPEN_READWRITE = 0x00000002;
constexpr int SQLITE_OPEN_CREATE = 0x00000004;
constexpr int SQLITE_OPEN_NOMUTEX = 0x00008000;
constexpr int SQLITE_UTF8 = 1;
constexpr int SQLITE_DETERMINISTIC = 0x000000800;
// El destructor "ya copie el dato, no lo guardes" de sqlite: cualquier valor
// distinto de NULL o SQLITE_STATIC (0) que no sea un puntero a funcion real.
using Destructor = void (*)(void*);
Destructor const SQLITE_TRANSIENT = (Destructor)(-1);

using Fn_open_v2 = int (*)(const char*, sqlite3**, int, const char*);
using Fn_close = int (*)(sqlite3*);
using Fn_prepare_v2 = int (*)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
using Fn_bind_text = int (*)(sqlite3_stmt*, int, const char*, int, Destructor);
using Fn_bind_int64 = int (*)(sqlite3_stmt*, int, sqlite3_int64);
using Fn_bind_int = int (*)(sqlite3_stmt*, int, int);
using Fn_bind_blob = int (*)(sqlite3_stmt*, int, const void*, int, Destructor);
using Fn_step = int (*)(sqlite3_stmt*);
using Fn_column_text = const unsigned char* (*)(sqlite3_stmt*, int);
using Fn_column_int64 = sqlite3_int64 (*)(sqlite3_stmt*, int);
using Fn_column_int = int (*)(sqlite3_stmt*, int);
using Fn_column_bytes = int (*)(sqlite3_stmt*, int);
using Fn_column_blob = const void* (*)(sqlite3_stmt*, int);
using Fn_finalize = int (*)(sqlite3_stmt*);
using Fn_exec = int (*)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
using Fn_create_function = int (*)(sqlite3*, const char*, int, int, void*,
                                   void (*)(sqlite3_context*, int, sqlite3_value**),
                                   void (*)(sqlite3_context*, int, sqlite3_value**),
                                   void (*)(sqlite3_context*));
using Fn_result_text = void (*)(sqlite3_context*, const char*, int, Destructor);
using Fn_value_text = const unsigned char* (*)(sqlite3_value*);
using Fn_reset = int (*)(sqlite3_stmt*);
using Fn_errmsg = const char* (*)(sqlite3*);
using Fn_last_insert_rowid = sqlite3_int64 (*)(sqlite3*);
using Fn_changes = int (*)(sqlite3*);
using Fn_free = void (*)(void*);

Fn_open_v2 p_open_v2;
Fn_close p_close;
Fn_prepare_v2 p_prepare_v2;
Fn_bind_text p_bind_text;
Fn_bind_int64 p_bind_int64;
Fn_bind_int p_bind_int;
Fn_bind_blob p_bind_blob;
Fn_step p_step;
Fn_column_text p_column_text;
Fn_column_int64 p_column_int64;
Fn_column_int p_column_int;
Fn_column_bytes p_column_bytes;
Fn_column_blob p_column_blob;
Fn_finalize p_finalize;
Fn_exec p_exec;
Fn_create_function p_create_function;
Fn_result_text p_result_text;
Fn_value_text p_value_text;
Fn_reset p_reset;
Fn_errmsg p_errmsg;
Fn_last_insert_rowid p_last_insert_rowid;
Fn_changes p_changes;
Fn_free p_free;

bool cargar_funciones() {
    static bool intentado = false;
    static bool ok = false;
    if (intentado) return ok;
    intentado = true;
    HMODULE h = LoadLibraryW(L"winsqlite3.dll");
    if (!h) return false;
    bool todo = true;
    auto cargar = [&](const char* nombre) -> FARPROC {
        FARPROC f = GetProcAddress(h, nombre);
        if (!f) todo = false;
        return f;
    };
    p_open_v2 = (Fn_open_v2)cargar("sqlite3_open_v2");
    p_close = (Fn_close)cargar("sqlite3_close");
    p_prepare_v2 = (Fn_prepare_v2)cargar("sqlite3_prepare_v2");
    p_bind_text = (Fn_bind_text)cargar("sqlite3_bind_text");
    p_bind_int64 = (Fn_bind_int64)cargar("sqlite3_bind_int64");
    p_bind_int = (Fn_bind_int)cargar("sqlite3_bind_int");
    p_bind_blob = (Fn_bind_blob)cargar("sqlite3_bind_blob");
    p_step = (Fn_step)cargar("sqlite3_step");
    p_column_text = (Fn_column_text)cargar("sqlite3_column_text");
    p_column_int64 = (Fn_column_int64)cargar("sqlite3_column_int64");
    p_column_int = (Fn_column_int)cargar("sqlite3_column_int");
    p_column_bytes = (Fn_column_bytes)cargar("sqlite3_column_bytes");
    p_column_blob = (Fn_column_blob)cargar("sqlite3_column_blob");
    p_finalize = (Fn_finalize)cargar("sqlite3_finalize");
    p_exec = (Fn_exec)cargar("sqlite3_exec");
    p_create_function = (Fn_create_function)cargar("sqlite3_create_function");
    p_result_text = (Fn_result_text)cargar("sqlite3_result_text");
    p_value_text = (Fn_value_text)cargar("sqlite3_value_text");
    p_reset = (Fn_reset)cargar("sqlite3_reset");
    p_errmsg = (Fn_errmsg)cargar("sqlite3_errmsg");
    p_last_insert_rowid = (Fn_last_insert_rowid)cargar("sqlite3_last_insert_rowid");
    p_changes = (Fn_changes)cargar("sqlite3_changes");
    p_free = (Fn_free)cargar("sqlite3_free");
    ok = todo;
    return ok;
}

// ---- plano(): sin acentos ni mayusculas ------------------------------------
// Tabla para Latin-1 Supplement + Latin Extended-A (U+00C0..U+017F), la
// misma logica se usa desde C++ (para la columna texto_plano) y registrada
// como funcion escalar SQL (para el lado de la busqueda).
const char* const TABLA_PLANO[0x180 - 0xC0] = {
    // C0..CF
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    // D0..DF
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
    // E0..EF
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    // F0..FF
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y",
    // 100..10F
    "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
    // 110..11F
    "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
    // 120..12F
    "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",
    // 130..13F
    "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",
    // 140..14F
    "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
    // 150..15F
    "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
    // 160..16F
    "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",
    // 170..17F
    "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
};

void utf8_agregar(std::string& s, unsigned cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

std::string plano_cpp(const std::string& entrada) {
    std::string salida;
    salida.reserve(entrada.size());
    size_t i = 0, n = entrada.size();
    while (i < n) {
        unsigned char c0 = (unsigned char)entrada[i];
        unsigned cp;
        int len;
        if (c0 < 0x80) {
            cp = c0;
            len = 1;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((c0 & 0x1Fu) << 6) | ((unsigned char)entrada[i + 1] & 0x3Fu);
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((c0 & 0x0Fu) << 12) | (((unsigned char)entrada[i + 1] & 0x3Fu) << 6) |
                 ((unsigned char)entrada[i + 2] & 0x3Fu);
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((c0 & 0x07u) << 18) | (((unsigned char)entrada[i + 1] & 0x3Fu) << 12) |
                 (((unsigned char)entrada[i + 2] & 0x3Fu) << 6) | ((unsigned char)entrada[i + 3] & 0x3Fu);
            len = 4;
        } else {
            // Byte invalido: lo dejamos pasar tal cual para no perder datos.
            cp = c0;
            len = 1;
        }
        i += len;
        if (cp >= 'A' && cp <= 'Z') {
            salida += (char)(cp - 'A' + 'a');
            continue;
        }
        if (cp >= 0xC0 && cp < 0x180) {
            const char* m = TABLA_PLANO[cp - 0xC0];
            if (m) {
                salida += m;
                continue;
            }
        }
        utf8_agregar(salida, cp);
    }
    return salida;
}

void xPlano(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    std::string entrada;
    if (argc >= 1) {
        const unsigned char* t = p_value_text(argv[0]);
        if (t) entrada = (const char*)t;
    }
    std::string salida = plano_cpp(entrada);
    p_result_text(ctx, salida.data(), (int)salida.size(), SQLITE_TRANSIENT);
}

// ---- conexion y ayudantes ---------------------------------------------------

sqlite3* g_db = nullptr;
std::mutex g_mu;

void crear_carpetas(const std::wstring& ruta) {
    // Crea las carpetas intermedias del archivo de `ruta` si no existen.
    size_t pos = 0;
    while ((pos = ruta.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
        std::wstring parcial = ruta.substr(0, pos);
        if (parcial.size() >= 2 && parcial.back() == L':') continue;  // "C:" solo
        CreateDirectoryW(parcial.c_str(), nullptr);
    }
}

void ejecutar(const char* sql) {
    char* err = nullptr;
    int rc = p_exec(g_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        red::registrar(std::string("cache: sqlite3_exec fallo: ") + (err ? err : "?") + " en: " + sql);
        if (err) p_free(err);
    }
}

// Envoltorio RAII para un statement preparado.
struct Stmt {
    sqlite3_stmt* st = nullptr;
    bool ok = false;

    explicit Stmt(const char* sql) {
        ok = g_db && p_prepare_v2(g_db, sql, -1, &st, nullptr) == SQLITE_OK;
    }
    ~Stmt() {
        if (st) p_finalize(st);
    }
    Stmt(const Stmt&) = delete;

    void bind_texto(int i, const std::string& s) { p_bind_text(st, i, s.data(), (int)s.size(), SQLITE_TRANSIENT); }
    void bind_ancho(int i, const std::wstring& s) { bind_texto(i, angosto(s)); }
    void bind_int64(int i, long long v) { p_bind_int64(st, i, v); }
    void bind_int(int i, int v) { p_bind_int(st, i, v); }

    // Ejecuta un paso; true si hay fila (SQLITE_ROW).
    bool fila() { return p_step(st) == SQLITE_ROW; }
    // Para INSERT/UPDATE/DELETE: corre hasta terminar.
    void correr() {
        int rc;
        do {
            rc = p_step(st);
        } while (rc == SQLITE_ROW);
        if (rc != SQLITE_DONE) red::registrar(std::string("cache: paso fallo: ") + (g_db ? p_errmsg(g_db) : "?"));
    }

    std::string col_texto(int i) {
        const unsigned char* t = p_column_text(st, i);
        return t ? std::string((const char*)t, (size_t)p_column_bytes(st, i)) : std::string();
    }
    std::wstring col_ancho(int i) { return ancho(col_texto(i)); }
    long long col_int64(int i) { return p_column_int64(st, i); }
    int col_int(int i) { return p_column_int(st, i); }
};

// ---- (de)serializacion auxiliar --------------------------------------------

std::string media_a_json(const Media& x) {
    std::string r = "{";
    r += "\"id\":" + std::to_string(x.id) + ",";
    r += "\"mime\":" + json_texto(x.mime) + ",";
    r += "\"nombre\":" + json_texto(x.nombre) + ",";
    r += "\"bytes\":" + std::to_string(x.bytes) + ",";
    r += "\"ancho\":" + std::to_string(x.ancho) + ",";
    r += "\"alto\":" + std::to_string(x.alto) + ",";
    r += "\"segundos\":" + std::to_string(x.segundos) + ",";
    r += "\"estado\":" + std::to_string(x.estado) + ",";
    r += std::string("\"miniatura\":") + (x.miniatura ? "true" : "false");
    if (!x.onda.empty()) {
        r += ",\"onda\":[";
        for (size_t i = 0; i < x.onda.size(); i++) r += (i ? "," : "") + std::to_string(x.onda[i]);
        r += "]";
    }
    r += "}";
    return r;
}

Media media_de_json(const Json& md) {
    Media x;
    x.id = md["id"].entero();
    x.mime = md["mime"].str();
    x.nombre = md["nombre"].str();
    x.bytes = md["bytes"].entero();
    x.ancho = (int)md["ancho"].entero();
    x.alto = (int)md["alto"].entero();
    x.segundos = (int)md["segundos"].entero();
    x.estado = (int)md["estado"].entero();
    x.miniatura = md["miniatura"].bul();
    const Json& onda = md["onda"];
    for (size_t k = 0; k < onda.largo(); k++) x.onda.push_back((unsigned char)onda[k].entero());
    return x;
}

// Reconstruye un Mensaje entero a partir del JSON (mismo formato que manda
// el server y que usa Mensaje::de_json en app.cpp). Va aparte -no llamamos a
// Mensaje::de_json- para que cache.cpp no dependa de linkear app.cpp.
Mensaje mensaje_de_json(const Json& j) {
    Mensaje m;
    m.id = j["id"].str();
    m.chat = j["chat"].str();
    m.remitente = j["remitente"].str();
    m.propio = j["propio"].bul();
    m.ts = j["ts"].entero();
    m.tipo = j["tipo"].str("texto");
    m.texto = ancho(j["texto"].str());
    m.cita_id = j["cita_id"].str();
    m.cita_remitente = j["cita_remitente"].str();
    m.cita_texto = ancho(j["cita_texto"].str());
    m.editado = j["editado"].bul();
    m.borrado = j["borrado"].bul();
    m.reenviado = j["reenviado"].bul();
    m.estado = (int)j["estado"].entero();
    if (j.esta("media")) m.media = media_de_json(j["media"]);
    return m;
}

// Serializa un Mensaje entero (se usa para el ultimo_json de un Chat).
std::string mensaje_a_json(const Mensaje& m) {
    std::string r = "{";
    r += "\"id\":" + json_texto(m.id) + ",";
    r += "\"chat\":" + json_texto(m.chat) + ",";
    r += "\"remitente\":" + json_texto(m.remitente) + ",";
    r += std::string("\"propio\":") + (m.propio ? "true" : "false") + ",";
    r += "\"ts\":" + std::to_string(m.ts) + ",";
    r += "\"tipo\":" + json_texto(m.tipo) + ",";
    r += "\"texto\":" + json_texto(angosto(m.texto)) + ",";
    r += "\"cita_id\":" + json_texto(m.cita_id) + ",";
    r += "\"cita_remitente\":" + json_texto(m.cita_remitente) + ",";
    r += "\"cita_texto\":" + json_texto(angosto(m.cita_texto)) + ",";
    r += std::string("\"editado\":") + (m.editado ? "true" : "false") + ",";
    r += std::string("\"borrado\":") + (m.borrado ? "true" : "false") + ",";
    r += std::string("\"reenviado\":") + (m.reenviado ? "true" : "false") + ",";
    r += "\"estado\":" + std::to_string(m.estado);
    if (m.media) r += ",\"media\":" + media_a_json(*m.media);
    r += "}";
    return r;
}

// Columnas de una fila de `mensajes`, en el orden que usan todas las
// consultas de abajo: chat,id,remitente,propio,ts,tipo,texto,cita_id,
// cita_remitente,cita_texto,editado,borrado,reenviado,estado,media_json.
const char* COLUMNAS_MENSAJE =
    "chat,id,remitente,propio,ts,tipo,texto,cita_id,cita_remitente,cita_texto,editado,borrado,reenviado,estado,"
    "media_json,reacciones_json";

Mensaje fila_a_mensaje(Stmt& st) {
    Mensaje m;
    m.chat = st.col_texto(0);
    m.id = st.col_texto(1);
    m.remitente = st.col_texto(2);
    m.propio = st.col_int(3) != 0;
    m.ts = st.col_int64(4);
    m.tipo = st.col_texto(5);
    m.texto = st.col_ancho(6);
    m.cita_id = st.col_texto(7);
    m.cita_remitente = st.col_texto(8);
    m.cita_texto = st.col_ancho(9);
    m.editado = st.col_int(10) != 0;
    m.borrado = st.col_int(11) != 0;
    m.reenviado = st.col_int(12) != 0;
    m.estado = st.col_int(13);
    std::string mj = st.col_texto(14);
    if (!mj.empty()) m.media = media_de_json(Json::parsear(mj));
    std::string rj = st.col_texto(15);
    if (!rj.empty()) {
        Json rs = Json::parsear(rj);
        for (size_t i = 0; i < rs.largo(); i++)
            m.reacciones.push_back({rs[i]["remitente"].str(), ancho(rs[i]["emoji"].str())});
    }
    return m;
}

std::string reacciones_a_json(const Mensaje& m) {
    if (m.reacciones.empty()) return "";
    std::string r = "[";
    for (size_t i = 0; i < m.reacciones.size(); i++)
        r += (i ? "," : "") + std::string("{\"remitente\":") + json_texto(m.reacciones[i].remitente) +
             ",\"emoji\":" + json_texto(angosto(m.reacciones[i].emoji)) + "}";
    return r + "]";
}

const char* ESQUEMA = R"sql(
CREATE TABLE IF NOT EXISTS chats (
    jid TEXT PRIMARY KEY,
    nombre TEXT,
    es_grupo INTEGER,
    tiene_foto INTEGER,
    ultimo_ts INTEGER,
    no_leidos INTEGER,
    archivado INTEGER,
    ultimo_json TEXT
);
CREATE TABLE IF NOT EXISTS contactos (
    jid TEXT PRIMARY KEY,
    nombre TEXT,
    tiene_foto INTEGER
);
CREATE TABLE IF NOT EXISTS mensajes (
    chat TEXT NOT NULL,
    id TEXT NOT NULL,
    remitente TEXT,
    propio INTEGER,
    ts INTEGER,
    tipo TEXT,
    texto TEXT,
    texto_plano TEXT,
    cita_id TEXT,
    cita_remitente TEXT,
    cita_texto TEXT,
    editado INTEGER,
    borrado INTEGER,
    reenviado INTEGER,
    estado INTEGER,
    media_json TEXT,
    PRIMARY KEY (chat, id)
);
CREATE INDEX IF NOT EXISTS ix_mensajes_chat_ts ON mensajes(chat, ts);
CREATE INDEX IF NOT EXISTS ix_mensajes_plano ON mensajes(texto_plano);
CREATE TABLE IF NOT EXISTS valores (
    clave TEXT PRIMARY KEY,
    valor TEXT
);
CREATE TABLE IF NOT EXISTS media_local (
    media_id INTEGER PRIMARY KEY,
    ruta TEXT,
    mime TEXT
);
)sql";

}  // namespace

namespace cache {

bool abrir(const std::wstring& ruta) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!cargar_funciones()) {
        red::registrar("cache: no se pudo cargar winsqlite3.dll");
        return false;
    }
    crear_carpetas(ruta);
    if (g_db) {
        p_close(g_db);
        g_db = nullptr;
    }
    std::string ruta_utf8 = angosto(ruta);
    int rc = p_open_v2(ruta_utf8.c_str(), &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                       nullptr);
    if (rc != SQLITE_OK) {
        red::registrar("cache: sqlite3_open_v2 fallo para " + ruta_utf8);
        if (g_db) {
            p_close(g_db);
            g_db = nullptr;
        }
        return false;
    }
    ejecutar("PRAGMA journal_mode=WAL;");
    ejecutar("PRAGMA synchronous=NORMAL;");
    ejecutar("PRAGMA busy_timeout=5000;");
    p_create_function(g_db, "plano", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, xPlano, nullptr, nullptr);
    ejecutar(ESQUEMA);
    // Columna agregada despues: si ya existe, el ALTER falla y no pasa nada.
    ejecutar("ALTER TABLE mensajes ADD COLUMN reacciones_json TEXT;");
    return true;
}

void cerrar() {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_db) {
        p_close(g_db);
        g_db = nullptr;
    }
}

// ---- chats ------------------------------------------------------------------

void guardar_chats(const std::vector<Chat>& chats) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    ejecutar("BEGIN;");
    ejecutar("DELETE FROM chats;");
    {
        Stmt st(
            "INSERT INTO chats(jid,nombre,es_grupo,tiene_foto,ultimo_ts,no_leidos,archivado,ultimo_json) "
            "VALUES(?,?,?,?,?,?,?,?);");
        if (st.ok) {
            for (const Chat& c : chats) {
                st.bind_texto(1, c.jid);
                st.bind_ancho(2, c.nombre);
                st.bind_int(3, c.es_grupo ? 1 : 0);
                st.bind_int(4, c.tiene_foto ? 1 : 0);
                st.bind_int64(5, c.ultimo_ts);
                st.bind_int(6, c.no_leidos);
                st.bind_int(7, c.archivado ? 1 : 0);
                st.bind_texto(8, c.ultimo ? mensaje_a_json(*c.ultimo) : std::string());
                st.correr();
                p_reset(st.st);
            }
        }
    }
    ejecutar("COMMIT;");
}

std::vector<Chat> leer_chats() {
    std::lock_guard<std::mutex> l(g_mu);
    std::vector<Chat> r;
    if (!g_db) return r;
    Stmt st("SELECT jid,nombre,es_grupo,tiene_foto,ultimo_ts,no_leidos,archivado,ultimo_json FROM chats;");
    if (!st.ok) return r;
    while (st.fila()) {
        Chat c;
        c.jid = st.col_texto(0);
        c.nombre = st.col_ancho(1);
        c.es_grupo = st.col_int(2) != 0;
        c.tiene_foto = st.col_int(3) != 0;
        c.ultimo_ts = st.col_int64(4);
        c.no_leidos = st.col_int(5);
        c.archivado = st.col_int(6) != 0;
        std::string uj = st.col_texto(7);
        if (!uj.empty()) c.ultimo = mensaje_de_json(Json::parsear(uj));
        r.push_back(std::move(c));
    }
    return r;
}

// ---- contactos ----------------------------------------------------------------

void guardar_contactos(const std::map<std::string, Contacto>& contactos) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    ejecutar("BEGIN;");
    ejecutar("DELETE FROM contactos;");
    {
        Stmt st("INSERT INTO contactos(jid,nombre,tiene_foto) VALUES(?,?,?);");
        if (st.ok) {
            for (const auto& [jid, k] : contactos) {
                st.bind_texto(1, jid);
                st.bind_ancho(2, k.nombre);
                st.bind_int(3, k.tiene_foto ? 1 : 0);
                st.correr();
                p_reset(st.st);
            }
        }
    }
    ejecutar("COMMIT;");
}

std::map<std::string, Contacto> leer_contactos() {
    std::lock_guard<std::mutex> l(g_mu);
    std::map<std::string, Contacto> r;
    if (!g_db) return r;
    Stmt st("SELECT jid,nombre,tiene_foto FROM contactos;");
    if (!st.ok) return r;
    while (st.fila()) {
        Contacto k;
        std::string jid = st.col_texto(0);
        k.nombre = st.col_ancho(1);
        k.tiene_foto = st.col_int(2) != 0;
        r[jid] = std::move(k);
    }
    return r;
}

// ---- mensajes -------------------------------------------------------------

void guardar_mensajes(const std::vector<Mensaje>& mensajes) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    ejecutar("BEGIN;");
    {
        Stmt st(
            "INSERT OR REPLACE INTO "
            "mensajes(chat,id,remitente,propio,ts,tipo,texto,texto_plano,cita_id,cita_remitente,cita_texto,"
            "editado,borrado,reenviado,estado,media_json,reacciones_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        if (st.ok) {
            for (const Mensaje& m : mensajes) {
                std::string texto_utf8 = angosto(m.texto);
                st.bind_texto(1, m.chat);
                st.bind_texto(2, m.id);
                st.bind_texto(3, m.remitente);
                st.bind_int(4, m.propio ? 1 : 0);
                st.bind_int64(5, m.ts);
                st.bind_texto(6, m.tipo);
                st.bind_texto(7, texto_utf8);
                st.bind_texto(8, plano_cpp(texto_utf8));
                st.bind_texto(9, m.cita_id);
                st.bind_texto(10, m.cita_remitente);
                st.bind_texto(11, angosto(m.cita_texto));
                st.bind_int(12, m.editado ? 1 : 0);
                st.bind_int(13, m.borrado ? 1 : 0);
                st.bind_int(14, m.reenviado ? 1 : 0);
                st.bind_int(15, m.estado);
                st.bind_texto(16, m.media ? media_a_json(*m.media) : std::string());
                st.bind_texto(17, reacciones_a_json(m));
                st.correr();
                p_reset(st.st);
            }
        }
    }
    ejecutar("COMMIT;");
}

std::vector<Mensaje> leer_mensajes(const std::string& chat, long long antes_ts, int limite) {
    std::lock_guard<std::mutex> l(g_mu);
    std::vector<Mensaje> r;
    if (!g_db) return r;
    std::string sql = std::string("SELECT ") + COLUMNAS_MENSAJE +
                      " FROM mensajes WHERE chat=? AND (?=0 OR ts<?) ORDER BY ts DESC LIMIT ?;";
    Stmt st(sql.c_str());
    if (!st.ok) return r;
    st.bind_texto(1, chat);
    st.bind_int64(2, antes_ts);
    st.bind_int64(3, antes_ts);
    st.bind_int(4, limite);
    while (st.fila()) r.push_back(fila_a_mensaje(st));
    std::reverse(r.begin(), r.end());  // quedo mas nuevo->mas viejo; lo damos ascendente
    return r;
}

bool hay_mensaje(const std::string& chat, const std::string& id) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return false;
    Stmt st("SELECT 1 FROM mensajes WHERE chat=? AND id=? LIMIT 1;");
    if (!st.ok) return false;
    st.bind_texto(1, chat);
    st.bind_texto(2, id);
    return st.fila();
}

void marcar_borrado(const std::string& chat, const std::string& id) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    Stmt st("UPDATE mensajes SET borrado=1 WHERE chat=? AND id=?;");
    if (!st.ok) return;
    st.bind_texto(1, chat);
    st.bind_texto(2, id);
    st.correr();
}

void editar_texto(const std::string& chat, const std::string& id, const std::wstring& texto) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    std::string texto_utf8 = angosto(texto);
    Stmt st("UPDATE mensajes SET texto=?, texto_plano=?, editado=1 WHERE chat=? AND id=?;");
    if (!st.ok) return;
    st.bind_texto(1, texto_utf8);
    st.bind_texto(2, plano_cpp(texto_utf8));
    st.bind_texto(3, chat);
    st.bind_texto(4, id);
    st.correr();
}

void poner_estado(const std::string& chat, const std::string& id, int estado) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    Stmt st("UPDATE mensajes SET estado=? WHERE chat=? AND id=? AND estado<?;");
    if (!st.ok) return;
    st.bind_int(1, estado);
    st.bind_texto(2, chat);
    st.bind_texto(3, id);
    st.bind_int(4, estado);
    st.correr();
}

long long ts_mas_viejo(const std::string& chat) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return 0;
    Stmt st("SELECT MIN(ts) FROM mensajes WHERE chat=?;");
    if (!st.ok) return 0;
    st.bind_texto(1, chat);
    if (st.fila()) return st.col_int64(0);
    return 0;
}

long long ts_mas_nuevo(const std::string& chat) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return 0;
    Stmt st("SELECT MAX(ts) FROM mensajes WHERE chat=?;");
    if (!st.ok) return 0;
    st.bind_texto(1, chat);
    if (st.fila()) return st.col_int64(0);
    return 0;
}

// ---- busqueda ---------------------------------------------------------------

std::vector<Mensaje> buscar(const std::wstring& texto, int limite) {
    std::lock_guard<std::mutex> l(g_mu);
    std::vector<Mensaje> r;
    if (!g_db) return r;
    std::string sql =
        std::string("SELECT ") + COLUMNAS_MENSAJE +
        " FROM mensajes WHERE texto_plano LIKE '%' || plano(?) || '%' ORDER BY ts DESC LIMIT ?;";
    Stmt st(sql.c_str());
    if (!st.ok) return r;
    st.bind_ancho(1, texto);
    st.bind_int(2, limite);
    while (st.fila()) r.push_back(fila_a_mensaje(st));
    return r;
}

std::vector<Mensaje> buscar_en(const std::string& chat, const std::wstring& texto, int limite) {
    std::lock_guard<std::mutex> l(g_mu);
    std::vector<Mensaje> r;
    if (!g_db) return r;
    std::string sql = std::string("SELECT ") + COLUMNAS_MENSAJE +
                      " FROM mensajes WHERE chat=? AND texto_plano LIKE '%' || plano(?) || '%' ORDER BY ts DESC "
                      "LIMIT ?;";
    Stmt st(sql.c_str());
    if (!st.ok) return r;
    st.bind_texto(1, chat);
    st.bind_ancho(2, texto);
    st.bind_int(3, limite);
    while (st.fila()) r.push_back(fila_a_mensaje(st));
    return r;
}

// ---- valores sueltos --------------------------------------------------------

std::string valor(const std::string& clave) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return std::string();
    Stmt st("SELECT valor FROM valores WHERE clave=?;");
    if (!st.ok) return std::string();
    st.bind_texto(1, clave);
    if (st.fila()) return st.col_texto(0);
    return std::string();
}

void guardar_valor(const std::string& clave, const std::string& valor) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    Stmt st("INSERT OR REPLACE INTO valores(clave,valor) VALUES(?,?);");
    if (!st.ok) return;
    st.bind_texto(1, clave);
    st.bind_texto(2, valor);
    st.correr();
}

// ---- media bajada a disco -----------------------------------------------------

std::wstring ruta_media(long long media_id) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return L"";
    Stmt st("SELECT ruta FROM media_local WHERE media_id=?;");
    if (!st.ok) return L"";
    st.bind_int64(1, media_id);
    if (st.fila()) return st.col_ancho(0);
    return L"";
}

void anotar_media(long long media_id, const std::wstring& ruta, const std::string& mime) {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_db) return;
    Stmt st("INSERT OR REPLACE INTO media_local(media_id,ruta,mime) VALUES(?,?,?);");
    if (!st.ok) return;
    st.bind_int64(1, media_id);
    st.bind_ancho(2, ruta);
    st.bind_texto(3, mime);
    st.correr();
}

}  // namespace cache
