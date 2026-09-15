#include "app.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "aviso.h"
#include "toast.h"
#include "webwa.h"
#include "cache.h"
#include "red.h"
#include "tema.h"
#include "ventana_ajustes.h"

namespace {


const float TITULO_H = 34.0f;
const float PAD_X = 9.0f;
const float PAD_Y = 6.0f;
const float HORA_TAM = 11.0f;
const float BARRA_H = 48.0f;    // la barra de "respondiendo a" / "editando"
const float ADJUNTO_H = 130.0f; // la vista previa del adjunto

bool con_imagen(const std::string& tipo) {
    return tipo == "imagen" || tipo == "video" || tipo == "gif" || tipo == "figurita";
}

SYSTEMTIME local_de(long long ts) {
    ULARGE_INTEGER u;
    u.QuadPart = (ULONGLONG)ts * 10000ULL + 116444736000000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME utc, loc;
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc);
    return loc;
}

int dia_de(long long ts) {
    SYSTEMTIME s = local_de(ts);
    return s.wYear * 10000 + s.wMonth * 100 + s.wDay;
}

long long ahora_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

// Encuentra URLs (http://, https://, www.) en un texto.
std::vector<VistaMensaje::Enlace> buscar_enlaces(const std::wstring& t) {
    std::vector<VistaMensaje::Enlace> r;
    size_t i = 0;
    while (i < t.size()) {
        size_t a = std::wstring::npos;
        size_t h1 = t.find(L"http://", i), h2 = t.find(L"https://", i), h3 = t.find(L"www.", i);
        a = std::min({h1, h2, h3});
        if (a == std::wstring::npos) break;
        if (a == h3 && a > 0 && iswalnum(t[a - 1])) {
            i = a + 4;
            continue;
        }
        size_t b = a;
        while (b < t.size() && !iswspace(t[b]) && t[b] != L'<' && t[b] != L'>' && t[b] != L'"' && t[b] != L'\'') b++;
        while (b > a && wcschr(L".,;:!?)]}", t[b - 1])) b--;
        if (b - a > 6) {
            std::wstring url = t.substr(a, b - a);
            if (url.rfind(L"www.", 0) == 0) url = L"https://" + url;
            r.push_back({a, b - a, url});
        }
        i = b > a ? b : a + 1;
    }
    return r;
}

// Cuantos emojis tiene un texto si es SOLO emojis (0 si hay otra cosa).
// Los modificadores (tono de piel, VS16, ZWJ, keycaps) no cuentan.
int solo_emojis(const std::wstring& t) {
    int n = 0;
    for (size_t i = 0; i < t.size(); i++) {
        unsigned cp = t[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < t.size()) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (t[i + 1] - 0xDC00);
            i++;
        }
        if (cp == 0xFE0F || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F)) continue;
        if (cp == ' ') continue;
        bool emoji = (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
                     cp == 0x2122 || cp == 0x2139 || (cp >= 0x2194 && cp <= 0x21AA) || cp == 0x231A || cp == 0x231B ||
                     cp == 0x2328 || cp == 0x23CF || (cp >= 0x23E9 && cp <= 0x23FA) || cp == 0x24C2 || cp == 0x25AA ||
                     cp == 0x25AB || cp == 0x25B6 || cp == 0x25C0 || (cp >= 0x25FB && cp <= 0x25FE) || cp == 0x3030 ||
                     cp == 0x303D || cp == 0x3297 || cp == 0x3299 || cp == 0xA9 || cp == 0xAE;
        if (!emoji) return 0;
        n++;
    }
    return n;
}

}  // namespace

std::wstring una_linea(std::wstring s) {
    for (auto& ch : s)
        if (ch == L'\n') ch = L' ';
    return s;
}

std::wstring nombre_tipo(const std::string& tipo) {
    if (tipo == "imagen") return L"\U0001F4F7 Photo";
    if (tipo == "video") return L"\U0001F3A5 Video";
    if (tipo == "gif") return L"\U0001F3A5 GIF";
    if (tipo == "audio") return L"\U0001F3B5 Audio";
    if (tipo == "nota") return L"\U0001F3A4 Voice message";
    if (tipo == "documento") return L"\U0001F4C4 Document";
    if (tipo == "figurita") return L"Sticker";
    if (tipo == "ubicacion") return L"\U0001F4CD Location";
    if (tipo == "contacto") return L"\U0001F464 Contact";
    if (tipo == "encuesta") return L"\U0001F4CA Poll";
    return L"";
}

std::wstring formatear_hora(long long ts) {
    SYSTEMTIME s = local_de(ts);
    wchar_t buf[16];
    swprintf(buf, 16, L"%02d:%02d", s.wHour, s.wMinute);
    return buf;
}

std::wstring formatear_dia(long long ts) {
    int d = dia_de(ts);
    long long hoy = ahora_ms();
    if (d == dia_de(hoy)) return L"Today";
    if (d == dia_de(hoy - 86400000LL)) return L"Yesterday";
    SYSTEMTIME s = local_de(ts);
    wchar_t buf[32];
    swprintf(buf, 32, L"%02d/%02d/%04d", s.wDay, s.wMonth, s.wYear);
    return buf;
}

std::wstring formatear_telefono(const std::string& jid) {
    std::string n = jid.substr(0, jid.find('@'));
    if (n.empty() || !isdigit((unsigned char)n[0])) return ancho(n);
    if (n == "0") return L"WhatsApp";
    if (n.size() < 6) return L"+" + ancho(n);
    std::wstring w = L"+";
    if (n.rfind("549", 0) == 0 && n.size() == 13) {
        // Argentina celular: +54 9 AREA NUMERO
        size_t area = 3;
        static const char* de_dos[] = {"11"};
        static const char* de_cuatro[] = {"2202", "2221", "2223", "2224", "2225", "2226", "2227", "2229", "2241", "2242", "2243", "2244", "2245", "2246", "2252", "2254", "2255", "2257", "2261", "2262", "2264", "2265", "2266", "2267", "2268", "2271", "2272", "2273", "2274", "2281", "2283", "2284", "2285", "2286", "2291", "2292", "2296", "2297", "2302", "2314", "2316", "2317", "2320", "2323", "2324", "2325", "2326", "2331", "2333", "2334", "2335", "2336", "2337", "2338", "2342", "2343", "2344", "2345", "2346", "2352", "2353", "2354", "2355", "2356", "2357", "2358", "2392", "2393", "2394", "2395", "2396", "2473", "2474", "2475", "2477", "2478", "2622", "2624", "2625", "2626", "2646", "2647", "2648", "2651", "2652", "2655", "2656", "2657", "2658", "2901", "2902", "2903", "2920", "2921", "2922", "2923", "2924", "2925", "2926", "2927", "2928", "2929", "2931", "2932", "2933", "2934", "2935", "2936", "2940", "2942", "2945", "2946", "2948", "2952", "2953", "2954", "2962", "2963", "2964", "2966", "2972", "2982", "2983", "3382", "3385", "3387", "3388", "3400", "3401", "3402", "3404", "3405", "3406", "3407", "3408", "3409", "3435", "3436", "3437", "3438", "3442", "3444", "3445", "3446", "3447", "3454", "3455", "3456", "3458", "3460", "3462", "3463", "3464", "3465", "3466", "3467", "3468", "3469", "3471", "3472", "3476", "3482", "3483", "3487", "3489", "3491", "3492", "3493", "3496", "3497", "3498", "3521", "3522", "3524", "3525", "3532", "3533", "3537", "3541", "3542", "3543", "3544", "3546", "3547", "3548", "3549", "3562", "3563", "3564", "3571", "3572", "3573", "3574", "3575", "3576", "3582", "3583", "3584", "3585", "3711", "3715", "3716", "3718", "3721", "3725", "3731", "3734", "3735", "3741", "3743", "3751", "3754", "3755", "3756", "3757", "3758", "3772", "3773", "3774", "3775", "3777", "3781", "3782", "3786", "3821", "3825", "3826", "3827", "3832", "3835", "3837", "3838", "3841", "3843", "3844", "3845", "3846", "3854", "3855", "3856", "3857", "3858", "3861", "3862", "3863", "3865", "3867", "3868", "3869", "3873", "3876", "3877", "3878", "3885", "3886", "3887", "3888", "3891", "3892", "3894"};
        std::string resto = n.substr(3);
        for (auto p : de_dos)
            if (resto.rfind(p, 0) == 0) area = 2;
        for (auto p : de_cuatro)
            if (resto.rfind(p, 0) == 0) area = 4;
        std::string local = resto.substr(area);
        w += L"54 9 " + ancho(resto.substr(0, area)) + L" " + ancho(local.substr(0, local.size() - 4)) + L"-" +
             ancho(local.substr(local.size() - 4));
        return w;
    }
    w += ancho(n.substr(0, 2)) + L" " + ancho(n.substr(2));
    return w;
}

Color color_de_nombre(const std::wstring& nombre) {
    unsigned h = 2166136261u;
    for (wchar_t c : nombre) h = (h ^ (unsigned)c) * 16777619u;
    float hue = (h % 360) / 360.0f, s = 0.70f, l = 0.68f;
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    auto canal = [&](float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
        return p;
    };
    return Color(canal(hue + 1.0f / 3), canal(hue), canal(hue - 1.0f / 3));
}

// ---- modelo ---------------------------------------------------------------

Mensaje Mensaje::de_json(const Json& j) {
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
    if (j.esta("media")) {
        const Json& md = j["media"];
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
        m.media = x;
    }
    const Json& rs = j["reacciones"];
    for (size_t i = 0; i < rs.largo(); i++)
        m.reacciones.push_back({rs[i]["remitente"].str(), ancho(rs[i]["emoji"].str())});
    return m;
}

Chat Chat::de_json(const Json& j) {
    Chat c;
    c.jid = j["jid"].str();
    c.nombre = ancho(j["nombre"].str());
    if (c.nombre.empty()) c.nombre = formatear_telefono(c.jid);
    c.es_grupo = j["es_grupo"].bul();
    c.tiene_foto = !j["foto"].str().empty();
    c.ultimo_ts = j["ultimo_ts"].entero();
    c.no_leidos = (int)j["no_leidos"].entero();
    c.archivado = j["archivado"].bul();
    if (j.esta("ultimo")) c.ultimo = Mensaje::de_json(j["ultimo"]);
    return c;
}

bool Desplazable::animar(float dt) {
    limitar();
    double d = objetivo - pos;
    if (std::abs(d) < 0.05) {
        pos = objetivo;
        return false;
    }
    // Persigue al objetivo con una constante de tiempo corta: se siente
    // como el scroll de un navegador.
    double k = 1.0 - std::exp(-dt / 0.055);
    pos += d * k;
    if (std::abs(objetivo - pos) < 0.05) pos = objetivo;
    return true;
}

ID2D1Bitmap1* Imagen::cuadro_actual(Gfx& g, unsigned long long ahora) {
    if (anim.cuadros.empty()) return bmp.Get();
    if (cuadros.size() != anim.cuadros.size()) cuadros.resize(anim.cuadros.size());
    if (empezo == 0) empezo = ahora;
    unsigned long long total = 0;
    for (int d : anim.duraciones) total += d;
    unsigned long long t = total ? (ahora - empezo) % total : 0;
    size_t i = 0;
    for (; i + 1 < anim.cuadros.size(); i++) {
        if (t < (unsigned long long)anim.duraciones[i]) break;
        t -= anim.duraciones[i];
    }
    if (!cuadros[i]) cuadros[i] = g.subir(anim, i);
    return cuadros[i].Get();
}

// ---- arranque -------------------------------------------------------------

void App::iniciar(HWND h) {
    hwnd = h;
    g.iniciar(h);
    campo.indicio = L"Type a message";
    campo.tamano = letra_chat + 0.5f;
    campo.al_enviar = [this] {
        if (adjunto) enviar_adjunto();
        else enviar_texto();
    };
    campo.al_cambiar = [this] {
        pedir_dibujo();
        teclear_presencia();
    };
    campo.al_escapar = [this] { escapar(); };
    campo.foco = true;
    buscador.indicio = L"Search or start new chat";
    buscador.tamano = 14.0f;
    buscador.alto_max = 32.0f;
    buscador.al_cambiar = [this] {
        pedir_dibujo();
        // La busqueda en el server espera a que dejes de escribir.
        SetTimer(hwnd, 3, 250, nullptr);
    };
    buscador.al_enviar = [this] { buscar_ahora(); };
    buscador.al_escapar = [this] { escapar(); };
    reproductor_ok = reproductor.iniciar(carpeta_exe());
    reproductor.al_cambiar = [this] { red::en_ui([this] { pedir_dibujo(); }); };
    aviso_estado = L"Connecting...";
    // Lo que quedo en la cache se muestra al instante; el server corrige.
    chats = cache::leer_chats();
    contactos = cache::leer_contactos();
    ordenar_chats();
    mi_jid = cache::valor("mi_jid");
    letra_chat = ajustes::actual().letra_chat;
    letra_lista = ajustes::actual().letra_lista;
    campo.tamano = letra_chat + 0.5f;
    cargar_chats();
}

void App::redimensionado() {
    g.redimensionar();
    ubicar_video_llamada();
    pedir_dibujo();
}

bool App::animando() {
    return !lista.quieto() || !conv.quieto() ||
           (!resaltado_id.empty() && ahora - resaltado_desde < 2000) ||
           alguien_escribe(chat_actual) || cargando_todo ||
           grab == Grab::Grabando ||
           ((!reproduciendo_id.empty() || grab_escuchando) && !reproductor.pausado() && !reproductor.terminado());
}

// ---- datos ----------------------------------------------------------------

void App::cargar_chats() {
    // Una recarga en vuelo a lo sumo; si piden otra mientras tanto, queda
    // una sola pendiente para despues.
    if (cargando_chats) {
        recarga_pendiente = true;
        return;
    }
    cargando_chats = true;
    red::en_fondo([this] {
        Respuesta est = red::obtener(L"/estado");
        Respuesta rc = red::obtener(L"/contactos");
        Respuesta r = red::obtener(L"/chats?ultimo=1");
        if (!r.ok()) {
            red::en_ui([this, r] {
                cargando_chats = false;
                aviso_estado = r.estado == 0 ? L"Cannot reach the server" : L"Server error " + std::to_wstring(r.estado);
                pedir_dibujo();
                // Sin server: se reintenta en unos segundos.
                SetTimer(hwnd, 2, 5000, nullptr);
            });
            return;
        }
        Json je = Json::parsear(est.cuerpo);
        Json jc = Json::parsear(rc.cuerpo);
        Json j = Json::parsear(r.cuerpo);
        {
            std::vector<Chat> lista_chats;
            for (size_t i = 0; i < j.largo(); i++) lista_chats.push_back(Chat::de_json(j[i]));
            std::map<std::string, Contacto> lista_contactos;
            for (size_t i = 0; i < jc.largo(); i++) {
                const Json& c = jc[i];
                Contacto k;
                std::string jid = c["jid"].str();
                std::string nn = c["nombre_agenda"].str();
                if (nn.empty()) nn = c["nombre_push"].str();
                k.nombre = nn.empty() ? formatear_telefono(jid) : ancho(nn);
                k.tiene_foto = !c["foto"].str().empty();
                lista_contactos[jid] = k;
            }
            cache::guardar_chats(lista_chats);
            cache::guardar_contactos(lista_contactos);
            cache::guardar_valor("mi_jid", je["jid"].str());
        }
        red::en_ui([this, je, jc, j] {
            cargando_chats = false;
            mi_jid = je["jid"].str();
            conectado = je["conectado"].bul();
            if (!escuchando) {
                // El cursor guardado: lo que paso con la app cerrada se aplica
                // a la cache al arrancar. Sin cursor (primera vez), desde ahora.
                std::string guardado = cache::valor("seq");
                seq_eventos = guardado.empty() ? je["seq"].entero() : atoll(guardado.c_str());
                escuchando = true;
                escuchar_eventos();
            }
            aviso_estado = conectado ? L"" : L"Server not connected to WhatsApp";
            bool logueado = je["logueado"].bul(true);
            if (!logueado && !sin_sesion) {
                sin_sesion = true;
                aviso_estado = L"";
                red::en_fondo([] { red::mandar_json(L"/vincular", "{}"); });
                SetTimer(hwnd, 10, 2500, nullptr);
            } else if (logueado && sin_sesion) {
                sin_sesion = false;
                qr_bmp.Reset();
                qr_datos.clear();
                KillTimer(hwnd, 10);
            }
            // Hubo una importacion masiva en el server: la cache de mensajes
            // ya no refleja lo que hay (mensajes nuevos en el medio, media
            // pegada a mensajes viejos). Se tira y se vuelve a pedir de a
            // paginas, como la primera vez. Los archivos locales de media
            // quedan (van por id de media, que no cambia).
            std::string importacion = je["importacion"].str();
            if (!importacion.empty() && importacion != cache::valor("importacion")) {
                cache::guardar_valor("importacion", importacion);
                std::string abierto = chat_actual;
                cache::borrar_mensajes();  // sincrono: abrir_chat lee la cache enseguida
                en_memoria.clear();
                en_memoria_orden.clear();
                if (!abierto.empty()) {
                    chat_actual.clear();
                    mensajes.clear();
                    vistas.clear();
                    abrir_chat(abierto);
                }
            }
            for (size_t i = 0; i < jc.largo(); i++) {
                const Json& c = jc[i];
                Contacto k;
                std::string jid = c["jid"].str();
                std::string n = c["nombre_agenda"].str();
                if (n.empty()) n = c["nombre_push"].str();
                k.nombre = n.empty() ? formatear_telefono(jid) : ancho(n);
                k.tiene_foto = !c["foto"].str().empty();
                contactos[jid] = k;
            }
            chats.clear();
            for (size_t i = 0; i < j.largo(); i++) chats.push_back(Chat::de_json(j[i]));
            ordenar_chats();
            pedir_dibujo();
            if (recarga_pendiente) {
                recarga_pendiente = false;
                cargar_chats();
            }
        });
    });
}

void App::ordenar_chats() {
    std::stable_sort(chats.begin(), chats.end(), [](const Chat& a, const Chat& b) { return a.ultimo_ts > b.ultimo_ts; });
}

const Chat* App::chat_de(const std::string& jid) const {
    for (auto& c : chats)
        if (c.jid == jid) return &c;
    return nullptr;
}

std::wstring App::nombre_de(const std::string& jid) {
    if (jid == mi_jid) return L"You";
    auto it = contactos.find(jid);
    if (it != contactos.end()) return it->second.nombre;
    if (const Chat* c = chat_de(jid)) return c->nombre;
    return formatear_telefono(jid);
}

void App::abrir_chat(const std::string& jid) {
    if (jid == chat_actual) return;
    if (seleccionando) terminar_seleccion();
    if (busca_chat_abierta) cerrar_busqueda_chat();
    if (!presencia_mandada.empty()) mandar_presencia("");
    {
        // Para que el telefono nos mande el "typing" de este contacto.
        std::string cuerpo = "{\"chat\":" + json_texto(jid) + "}";
        red::en_fondo([cuerpo] { red::mandar_json(L"/presencia", cuerpo); });
    }
    // El borrador del chat que se deja queda guardado.
    if (!chat_actual.empty()) {
        borradores[chat_actual] = campo.texto;
        borradores_adjunto[chat_actual] = adjunto;
        recordar_chat();
    }
    chat_actual = jid;
    mensajes.clear();
    vistas.clear();
    layout_pendiente = 0;
    layout_gen++;
    tandas_listas.clear();
    cargando_mensajes = true;
    hay_mas_viejos = true;
    conv = Desplazable();
    respondiendo.reset();
    editando.reset();
    sel_msg = -1;
    info_abierto = false;
    info_pila.clear();
    emojis_abierto = false;
    campo.poner(borradores[jid]);
    adjunto = borradores_adjunto[jid];
    campo.foco = true;
    int cuantos = ajustes::actual().mensajes_por_chat;
    if (cuantos <= 0) cuantos = 1000000;
    pedir_dibujo();
    std::string mio = jid;
    long long ultimo_lista = 0;
    for (auto& c : chats)
        if (c.jid == jid) ultimo_lista = c.ultimo_ts;

    // Si el server lo reescribio, se trae de nuevo entero.
    if (chats_para_refrescar.count(jid)) {
        refrescar_chat_del_server(jid);
        return;
    }
    // Si ya lo tuvimos abierto, esta en memoria: se muestra ya.
    auto em = en_memoria.find(jid);
    if (em != en_memoria.end()) {
        mensajes = std::move(em->second.mensajes);
        hay_mas_viejos = em->second.hay_mas_viejos;
        en_memoria.erase(em);
        en_memoria_orden.erase(std::remove(en_memoria_orden.begin(), en_memoria_orden.end(), jid), en_memoria_orden.end());
        cargando_mensajes = false;
        armar_vistas();
        bajar_al_final(true);
        marcar_leido(jid);
        // Solo si la lista dice que hay algo mas nuevo, se pide eso.
        long long mas_nuevo = mensajes.empty() ? 0 : mensajes.back().ts;
        if (ultimo_lista > mas_nuevo) {
            red::en_fondo([this, mio, mas_nuevo] {
                Respuesta r = red::obtener(L"/mensajes?chat=" + ancho(mio) + L"&limite=100000&desde=" + std::to_wstring(mas_nuevo + 1), 120000);
                Json j = Json::parsear(r.cuerpo);
                std::vector<Mensaje> nuevos;
                for (size_t i = 0; i < j.largo(); i++) nuevos.push_back(Mensaje::de_json(j[i]));
                if (r.ok()) cache::guardar_mensajes(nuevos);
                red::en_ui([this, mio, nuevos] {
                    if (mio != chat_actual) return;
                    for (auto& m : nuevos) agregar_mensaje(m);
                    pedir_dibujo();
                });
            });
        }
        return;
    }

    abierto_en = GetTickCount64();
    red::en_fondo([this, mio, cuantos, ultimo_lista] {
        unsigned long long t0 = GetTickCount64();
        // La cache manda. Primero los ultimos 200 (se ven al toque), despues
        // el resto se lee y se agrega arriba sin mover la vista. Al server
        // solo se le pide lo que falta: mas viejos que no estan cacheados,
        // o mas nuevos si la lista dice que hay.
        int primera = std::min(cuantos, 200);
        std::vector<Mensaje> de_cache = cache::leer_mensajes(mio, 0, primera);
        long long mas_nuevo = de_cache.empty() ? 0 : de_cache.back().ts;
        long long mas_viejo = de_cache.empty() ? 0 : de_cache.front().ts;
        bool ok = true;
        std::vector<Mensaje> nuevos;
        // Lo que llego despues de lo cacheado (la app cerrada sin log, o historia).
        if (mas_nuevo > 0 && (ultimo_lista > mas_nuevo || resync_pendiente)) {
            std::wstring url = L"/mensajes?chat=" + ancho(mio) + L"&limite=100000&desde=" + std::to_wstring(mas_nuevo + 1);
            Respuesta r = red::obtener(url, 120000);
            ok = r.ok();
            Json j = Json::parsear(r.cuerpo);
            for (size_t i = 0; i < j.largo(); i++) nuevos.push_back(Mensaje::de_json(j[i]));
            if (ok) cache::guardar_mensajes(nuevos);
        }
        std::vector<Mensaje> primeros;
        std::unordered_set<std::string> vistos;
        for (auto* lista : {&de_cache, &nuevos})
            for (auto& m : *lista)
                if (vistos.insert(m.id).second) primeros.push_back(m);
        int total = (int)primeros.size();
        red::en_ui([this, mio, primeros, ok] {
            if (mio != chat_actual) return;
            if (!ok && primeros.empty()) {
                cargando_mensajes = false;
                aviso_estado = L"Cannot reach the server";
                pedir_dibujo();
                return;
            }
            mensajes = primeros;
            armar_vistas();
            bajar_al_final(true);
            marcar_leido(mio);
            pedir_dibujo();
        });
        if (!ok) {
            red::en_ui([this] { cargando_mensajes = false; });
            return;
        }
        // Segunda tanda: el resto de la cache, y despues el server si falta.
        std::vector<Mensaje> resto;
        if (cuantos > total && mas_viejo > 0) resto = cache::leer_mensajes(mio, mas_viejo, cuantos - total);
        red::registrar("cache leida: " + std::to_string(resto.size() + de_cache.size()) + " mensajes en " + std::to_string(GetTickCount64() - t0) + " ms");
        if (!resto.empty()) {
            total += (int)resto.size();
            mas_viejo = resto.front().ts;
            red::en_ui([this, mio, resto] {
                if (mio != chat_actual) return;
                anteponer(resto);
            });
        }
        bool agotado = false;
        // Del server, de a paginas de 20k, hasta completar lo pedido o agotar el chat.
        while (cuantos > total) {
            int faltan = std::min(cuantos - total, 20000);
            std::wstring url = L"/mensajes?chat=" + ancho(mio) + L"&limite=" + std::to_wstring(faltan);
            if (mas_viejo > 0) url += L"&antes=" + std::to_wstring(mas_viejo);
            Respuesta r = red::obtener(url, 300000);
            if (!r.ok()) break;
            std::vector<Mensaje> viejos;
            Json j = Json::parsear(r.cuerpo);
            for (size_t i = 0; i < j.largo(); i++) viejos.push_back(Mensaje::de_json(j[i]));
            for (size_t i = 0; i < viejos.size(); i += 500)
                cache::guardar_mensajes(std::vector<Mensaje>(viejos.begin() + i, viejos.begin() + std::min(viejos.size(), i + 500)));
            agotado = (int)viejos.size() < faltan;
            if (viejos.empty()) break;
            total += (int)viejos.size();
            mas_viejo = viejos.front().ts;
            red::en_ui([this, mio, viejos] {
                if (mio != chat_actual) return;
                anteponer(viejos);
            });
            if (agotado) break;
        }
        red::en_ui([this, mio, agotado] {
            if (mio != chat_actual) return;
            cargando_mensajes = false;
            hay_mas_viejos = !agotado;
            pedir_dibujo();
        });
    });
}

// Los mensajes del chat que se deja quedan en memoria (hasta 6 chats).
void App::recordar_chat() {
    if (chat_actual.empty() || mensajes.empty()) return;
    en_memoria[chat_actual] = {std::move(mensajes), hay_mas_viejos};
    en_memoria_orden.erase(std::remove(en_memoria_orden.begin(), en_memoria_orden.end(), chat_actual), en_memoria_orden.end());
    en_memoria_orden.push_back(chat_actual);
    while (en_memoria_orden.size() > 6) {
        en_memoria.erase(en_memoria_orden.front());
        en_memoria_orden.erase(en_memoria_orden.begin());
    }
    mensajes.clear();
}

void App::anteponer(const std::vector<Mensaje>& viejos) {
    if (viejos.empty()) return;
    // Sin repetidos con lo que ya esta.
    std::unordered_set<std::string> ids;
    for (auto& m : mensajes) ids.insert(m.id);
    std::vector<Mensaje> limpios;
    for (auto& m : viejos)
        if (!ids.count(m.id)) limpios.push_back(m);
    if (limpios.empty()) return;
    mensajes.insert(mensajes.begin(), limpios.begin(), limpios.end());
    vistas.insert(vistas.begin(), limpios.size(), VistaMensaje());
    layout_pendiente += limpios.size();
    if (sel_msg >= 0) sel_msg += (int)limpios.size();
    lanzar_armado();
    // El que era el primero ahora tiene un anterior: su divisor puede cambiar.
    if (layout_pendiente < vistas.size()) {
        float antes = vistas[layout_pendiente].alto;
        armar_vista(layout_pendiente);
        double d = vistas[layout_pendiente].alto - antes;
        conv.pos += d;
        conv.objetivo += d;
        conv.max += d;
    }
    pedir_dibujo();
}

void App::cargar_mas_viejos() {
    if (cargando_mensajes || !hay_mas_viejos || mensajes.empty()) return;
    cargando_mensajes = true;
    std::string mio = chat_actual;
    long long antes = mensajes.front().ts;
    int pref = ajustes::actual().mensajes_por_chat;
    int cuantos = pref <= 0 ? 5000 : std::max(60, std::min(pref, 2000));
    red::en_fondo([this, mio, antes, cuantos] {
        // Si la cache tiene una pagina entera anterior, va esa; si no, el server.
        std::vector<Mensaje> viejos = cache::leer_mensajes(mio, antes, cuantos);
        bool del_server = false;
        if ((int)viejos.size() < cuantos) {
            Respuesta r = red::obtener(L"/mensajes?chat=" + ancho(mio) + L"&antes=" + std::to_wstring(antes) + L"&limite=" + std::to_wstring(cuantos), 120000);
            Json j = Json::parsear(r.cuerpo);
            if (r.ok()) {
                viejos.clear();
                for (size_t i = 0; i < j.largo(); i++) viejos.push_back(Mensaje::de_json(j[i]));
                cache::guardar_mensajes(viejos);
                del_server = true;
            }
        }
        red::en_ui([this, mio, viejos, del_server, cuantos] {
            if (mio != chat_actual) return;
            cargando_mensajes = false;
            hay_mas_viejos = (int)viejos.size() >= cuantos || !del_server;
            if (viejos.empty()) {
                hay_mas_viejos = false;
                return;
            }
            anteponer(viejos);
        });
    });
}

// "typing..." / "recording audio..." del chat, o "" si no hay nadie (o ya paso).
std::wstring App::texto_escribiendo(const std::string& chat) {
    auto it = escribiendo.find(chat);
    if (it == escribiendo.end()) return L"";
    if (it->second.hasta <= GetTickCount64()) {
        escribiendo.erase(it);
        return L"";
    }
    std::wstring t = it->second.grabando ? L"recording audio..." : L"typing...";
    const Chat* c = chat_de(chat);
    if (c && c->es_grupo) t = nombre_de(it->second.quien) + L" is " + t;
    return t;
}

// Le avisa al server (y por el a WhatsApp) que estamos escribiendo/grabando.
void App::mandar_presencia(const std::string& estado) {
    if (chat_actual.empty()) return;
    presencia_mandada = estado;
    presencia_ts = GetTickCount64();
    std::string cuerpo = "{\"chat\":" + json_texto(chat_actual) + ",\"estado\":" + json_texto(estado) + "}";
    red::en_fondo([cuerpo] { red::mandar_json(L"/escribiendo", cuerpo); });
    // Si dejamos de escribir, a los 5 s se manda "paused" (timer 6).
    if (!estado.empty()) SetTimer(hwnd, 6, 5000, nullptr);
    else KillTimer(hwnd, 6);
}

void App::teclear_presencia() {
    if (campo.texto.empty()) {
        if (presencia_mandada == "typing") mandar_presencia("");
        return;
    }
    // WhatsApp repite el composing cada tanto; con uno cada 4 s alcanza.
    if (presencia_mandada != "typing" || GetTickCount64() - presencia_ts > 4000) mandar_presencia("typing");
    else SetTimer(hwnd, 6, 5000, nullptr);
}

void App::marcar_leido(const std::string& jid, const std::vector<std::string>& ids) {
    bool habia = false;
    for (auto& c : chats)
        if (c.jid == jid && c.no_leidos > 0) {
            c.no_leidos = 0;
            habia = true;
        }
    if (!habia && ids.empty()) return;
    std::string cuerpo = "{\"chat\":" + json_texto(jid);
    if (!ids.empty()) {
        cuerpo += ",\"ids\":[";
        for (size_t i = 0; i < ids.size(); i++) cuerpo += (i ? "," : "") + json_texto(ids[i]);
        cuerpo += "]";
    }
    cuerpo += "}";
    red::en_fondo([cuerpo] { red::mandar_json(L"/leido", cuerpo); });
}

// Al volver a la ventana, lo que llego mientras no estaba se marca leido.
void App::ventana_activada() {
    if (chat_actual.empty()) return;
    std::vector<std::string> ids;
    for (auto& m : mensajes)
        if (!m.propio && m.ts > ahora_ms() - 3600000LL) ids.push_back(m.id);
    if (!ids.empty()) marcar_leido(chat_actual, ids);
}

void App::enviar_texto() {
    std::wstring t = campo.texto;
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\n')) t.pop_back();
    if (t.empty() || chat_actual.empty()) return;
    campo.poner(L"");
    if (!presencia_mandada.empty()) mandar_presencia("");
    std::string chat = chat_actual;
    if (editando) {
        std::string id = editando->id;
        editando.reset();
        std::string cuerpo = "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"texto\":" + json_texto(angosto(t)) + "}";
        red::en_fondo([cuerpo] { red::mandar_json(L"/editar", cuerpo); });
        return;
    }
    std::string cita = respondiendo ? respondiendo->id : "";
    respondiendo.reset();
    std::string cuerpo = "{\"chat\":" + json_texto(chat) + ",\"texto\":" + json_texto(angosto(t)) +
                         (cita.empty() ? "" : ",\"cita_id\":" + json_texto(cita)) + "}";
    red::en_fondo([this, chat, cuerpo, t] {
        Respuesta r = red::mandar_json(L"/enviar", cuerpo);
        Json j = Json::parsear(r.cuerpo);
        bool ok = r.ok();
        red::en_ui([this, chat, j, ok, t] {
            if (!ok) {
                aviso_estado = L"Could not send: " + ancho(j["error"].str("no response"));
                if (chat == chat_actual && campo.texto.empty()) campo.poner(t);
                pedir_dibujo();
                return;
            }
            agregar_mensaje(Mensaje::de_json(j));
            pedir_dibujo();
        });
    });
}

void App::escuchar_eventos() {
    red::en_fondo([this] {
        for (;;) {
            long long desde = seq_eventos;
            Respuesta r = red::obtener(L"/eventos?desde=" + std::to_wstring(desde), 40000);
            if (!r.ok()) {
                red::en_ui([this, r] {
                    if (r.estado == 0 && aviso_estado.empty()) aviso_estado = L"Cannot reach the server";
                    pedir_dibujo();
                });
                Sleep(3000);
                continue;
            }
            Json j = Json::parsear(r.cuerpo);
            if (j["resync"].bul()) {
                // El server ya no tiene el log desde nuestro cursor: se
                // arranca desde ahora y cada chat se completa al abrirlo.
                red::en_ui([this] {
                    resync_pendiente = true;
                    cargar_chats();
                });
                Respuesta est = red::obtener(L"/estado");
                seq_eventos = Json::parsear(est.cuerpo)["seq"].entero();
                cache::guardar_valor("seq", std::to_string(seq_eventos));
                Sleep(1000);
                continue;
            }
            const Json& lista_ev = j["eventos"];
            if (lista_ev.largo() == 0) continue;
            red::en_ui([this, j] {
                if (!aviso_estado.empty() && aviso_estado.rfind(L"Cannot", 0) == 0) aviso_estado.clear();
                const Json& l = j["eventos"];
                bool recargar = false;
                for (size_t i = 0; i < l.largo(); i++) {
                    if (aplicar_evento(l[i])) recargar = true;
                    seq_eventos = std::max(seq_eventos, l[i]["seq"].entero());
                }
                if (recargar) cargar_chats();
                cache::guardar_valor("seq", std::to_string(seq_eventos));
                pedir_dibujo();
            });
            // El hilo espera a que la UI anote el cursor: alcanza con seguir.
            long long ultimo = lista_ev[lista_ev.largo() - 1]["seq"].entero();
            while (seq_eventos < ultimo) Sleep(5);
        }
    });
}

bool App::aplicar_evento(const Json& e) {
    std::string tipo = e["tipo"].str();
    std::string chat = e["chat"].str();
    const Json& d = e["datos"];
    if (tipo == "mensaje") {
        Mensaje m = Mensaje::de_json(d);
        cache::guardar_mensajes({m});
        bool hay = chat_de(m.chat) != nullptr;
        // Aviso en la bandeja si no estoy mirando ese chat.
        if (!m.propio && !aviso::esta_al_frente(hwnd)) {
            const Chat* c = chat_de(m.chat);
            std::wstring titulo = c ? c->nombre : nombre_de(m.chat);
            std::wstring texto = m.texto.empty() ? nombre_tipo(m.tipo) : una_linea(m.texto);
            if (c && c->es_grupo) texto = nombre_de(m.remitente) + L": " + texto;
            if (ajustes::actual().notificaciones) {
                bool con_foto = (c && c->tiene_foto) || (contactos.count(m.chat) && contactos[m.chat].tiene_foto);
                toast::mostrar(titulo, texto, m.chat, m.id, con_foto ? L"/foto/" + ancho(m.chat) : L"");
            }
        }
        if (hay && !m.propio && m.chat != chat_actual)
            for (auto& c : chats)
                if (c.jid == m.chat) c.no_leidos++;
        agregar_mensaje(m);
        if (m.chat == chat_actual && !m.propio && GetForegroundWindow() == hwnd) marcar_leido(m.chat, {m.id});
        return !hay;
    } else if (tipo == "acuse") {
        std::string id = d["id"].str();
        int estado = (int)d["estado"].entero();
        cache::poner_estado(chat, id, estado);
        for (auto& c : chats)
            if (c.jid == chat && c.ultimo && c.ultimo->id == id) c.ultimo->estado = std::max(c.ultimo->estado, estado);
        if (chat != chat_actual) return false;
        for (auto& x : mensajes)
            if (x.id == id) x.estado = std::max(x.estado, estado);
    } else if (tipo == "editado" || tipo == "borrado") {
        std::string id = d["id"].str();
        if (tipo == "borrado") cache::marcar_borrado(chat, id);
        else cache::editar_texto(chat, id, ancho(d["texto"].str()));
        for (auto& c : chats)
            if (c.jid == chat && c.ultimo && c.ultimo->id == id) {
                if (tipo == "borrado") c.ultimo->borrado = true;
                else c.ultimo->texto = ancho(d["texto"].str());
            }
        if (chat != chat_actual) return false;
        for (size_t i = 0; i < mensajes.size(); i++)
            if (mensajes[i].id == id) {
                if (tipo == "borrado") mensajes[i].borrado = true;
                else {
                    mensajes[i].texto = ancho(d["texto"].str());
                    mensajes[i].editado = true;
                }
                armar_vista(i);
            }
    } else if (tipo == "reaccion") {
        if (chat != chat_actual) return false;
        std::string id = d["id"].str(), quien = d["remitente"].str();
        std::wstring emoji = ancho(d["emoji"].str());
        for (size_t i = 0; i < mensajes.size(); i++)
            if (mensajes[i].id == id) {
                auto& rs = mensajes[i].reacciones;
                rs.erase(std::remove_if(rs.begin(), rs.end(), [&](const Reaccion& r) { return r.remitente == quien; }), rs.end());
                if (!emoji.empty()) rs.push_back({quien, emoji});
                cache::guardar_mensajes({mensajes[i]});
                armar_vista(i);
            }
    } else if (tipo == "media") {
        long long id = d["id"].entero();
        int estado = (int)d["estado"].entero();
        bool mini = d["miniatura"].bul();
        for (auto& x : mensajes)
            if (x.media && x.media->id == id) {
                x.media->estado = estado;
                if (mini) x.media->miniatura = true;
            }
        if (estado == 1) {
            imagenes.erase("media:" + std::to_string(id));
            imagenes.erase("mini:" + std::to_string(id));
        }
    } else if (tipo == "historia") {
        // El server cambio la historia de ese chat: lo cacheado no alcanza.
        if (!chat.empty()) {
            en_memoria.erase(chat);
            en_memoria_orden.erase(std::remove(en_memoria_orden.begin(), en_memoria_orden.end(), chat), en_memoria_orden.end());
            if (chat == chat_actual) {
                // Una importacion manda muchos seguidos: se junta todo en una recarga.
                refresco_pendiente = true;
                SetTimer(hwnd, 7, 3000, nullptr);
            } else chats_para_refrescar.insert(chat);
        }
        return true;
    } else if (tipo == "chat" || tipo == "chats" || tipo == "contactos" || tipo == "contacto") {
        return true;
    } else if (tipo == "leido") {
        for (auto& c : chats)
            if (c.jid == chat) c.no_leidos = 0;
    } else if (tipo == "llamada") {
        std::string estado = d["estado"].str(), id = d["id"].str();
        long long ts = e["ts"].entero();
        if (estado == "entrante" && ts > ahora_ms() - 60000) {
            std::string de = d["de"].str();
            const Chat* c = chat_de(chat);
            std::wstring titulo = c ? c->nombre : nombre_de(chat);
            std::wstring texto = d["video"].bul() ? L"Incoming video call" : L"Incoming voice call";
            if (c && c->es_grupo) texto += L" from " + nombre_de(de);
            bool con_foto = (c && c->tiene_foto) || (contactos.count(chat) && contactos[chat].tiene_foto);
            llamadas_entrantes[id] = {chat, d["video"].bul()};
            toast::puede_atender(llamadas_disponibles());
            toast::llamada(titulo, texto, chat, id, con_foto ? L"/foto/" + ancho(chat) : L"");
        } else if (estado != "entrante") {
            toast::llamada_terminada(id);
            llamadas_entrantes.erase(id);
        }
    } else if (tipo == "escribiendo") {
        // Efimero: si el evento es viejo (reconexion, arranque) no vale.
        long long ts = e["ts"].entero();
        std::string estado = d["estado"].str();
        bool estaba_al_final = chat == chat_actual && al_final();
        if (estado == "nada" || ts < ahora_ms() - 20000) escribiendo.erase(chat);
        else {
            escribiendo[chat] = {d["quien"].str(), estado == "grabando", GetTickCount64() + 12000};
            SetTimer(hwnd, 4, 12500, nullptr);  // para que se apague solo
        }
        if (chat == chat_actual && inicio.size() == vistas.size() + 1) {
            conv.max = std::max(0.0, alto_contenido() - (g.alto - alto_cabecera() - alto_pie));
            if (estaba_al_final) conv.ir(conv.max, false);
        }
        pedir_dibujo();
    } else if (tipo == "sesion") {
        conectado = d["conectado"].bul();
        aviso_estado = conectado ? L"" : L"Server not connected to WhatsApp";
    }
    return false;
}

// ---- layout de mensajes ---------------------------------------------------

void App::armar_vistas() {
    vistas.assign(mensajes.size(), VistaMensaje());
    layout_pendiente = mensajes.size();
    // Lo ultimo (lo que se ve) se arma ya; el resto, en hilos de fondo.
    avanzar_layouts(120, 1000.0f);
    lanzar_armado();
}

// Copia los mensajes pendientes y los reparte en tandas entre un hilo por
// nucleo. Cada tanda vuelve al hilo de UI y se aplica cuando es contigua a
// lo ya armado (de abajo hacia arriba), asi la vista no se mueve.
void App::lanzar_armado() {
    layout_gen++;
    tandas_listas.clear();
    if (layout_pendiente == 0) return;
    unsigned gen = layout_gen;
    auto copia = std::make_shared<std::vector<Mensaje>>(mensajes.begin(), mensajes.begin() + layout_pendiente);
    const Chat* c = chat_de(chat_actual);
    bool es_grupo = c && c->es_grupo;
    // Los nombres se resuelven aca, en el hilo de UI, para no leer los mapas desde otros hilos.
    auto nombres = std::make_shared<std::unordered_map<std::string, std::wstring>>();
    for (auto& m : *copia) {
        if (!nombres->count(m.remitente)) (*nombres)[m.remitente] = nombre_de(m.remitente);
        if (!m.cita_remitente.empty() && !nombres->count(m.cita_remitente)) (*nombres)[m.cita_remitente] = nombre_de(m.cita_remitente);
    }
    float W = w_conv();
    size_t n = copia->size();
    const size_t TANDA = 3000;
    auto siguiente = std::make_shared<std::atomic<size_t>>(n);  // el final de la proxima tanda a tomar
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int hilos = std::clamp((int)si.dwNumberOfProcessors, 2, 16);
    for (int h = 0; h < hilos; h++) {
        red::en_fondo([this, gen, copia, nombres, es_grupo, W, siguiente, TANDA] {
            auto nombre = [nombres](const std::string& j) {
                auto it = nombres->find(j);
                return it != nombres->end() ? it->second : formatear_telefono(j);
            };
            for (;;) {
                // Tomar la tanda que termina en `fin` (de atras para adelante).
                size_t fin = siguiente->load();
                if (fin == 0) return;
                size_t inicio = fin > TANDA ? fin - TANDA : 0;
                if (!siguiente->compare_exchange_strong(fin, inicio)) continue;
                if (gen != layout_gen) return;
                std::vector<VistaMensaje> tanda(fin - inicio);
                for (size_t i = inicio; i < fin; i++)
                    armar_vista_core((*copia)[i], i > 0 ? &(*copia)[i - 1] : nullptr, es_grupo, nombre, W, tanda[i - inicio]);
                red::en_ui([this, gen, fin, tanda = std::move(tanda)]() mutable {
                    if (gen != layout_gen) return;
                    tandas_listas[fin] = std::move(tanda);
                    aplicar_tandas();
                });
            }
        });
    }
}

void App::aplicar_tandas() {
    double agregado = 0;
    for (;;) {
        auto it = tandas_listas.find(layout_pendiente);
        if (it == tandas_listas.end()) break;
        std::vector<VistaMensaje>& tanda = it->second;
        size_t inicio = layout_pendiente - tanda.size();
        for (size_t k = 0; k < tanda.size(); k++) {
            vistas[inicio + k] = std::move(tanda[k]);
            agregado += vistas[inicio + k].alto;
        }
        layout_pendiente = inicio;
        tandas_listas.erase(it);
    }
    if (agregado > 0) {
        conv.max += agregado;
        conv.pos += agregado;
        conv.objetivo += agregado;
        pedir_dibujo();
    }
    if (layout_pendiente == 0 && abierto_en) {
        red::registrar("layouts listos: " + std::to_string(vistas.size()) + " en " + std::to_string(GetTickCount64() - abierto_en) + " ms desde abrir");
    }
}

double App::avanzar_layouts(int cuantos, float ms_max) {
    if (layout_pendiente > vistas.size()) layout_pendiente = vistas.size();
    if (vistas.size() != mensajes.size()) {
        vistas.resize(mensajes.size());
        layout_pendiente = std::min(layout_pendiente, vistas.size());
    }
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    double agregado = 0;  // en double: en float el error se acumula frame a frame
    while (layout_pendiente > 0 && cuantos-- > 0) {
        layout_pendiente--;
        armar_vista(layout_pendiente);
        agregado += vistas[layout_pendiente].alto;
        QueryPerformanceCounter(&t1);
        if ((t1.QuadPart - t0.QuadPart) * 1000.0f / f.QuadPart > ms_max) break;
    }
    return agregado;
}

void App::armar_vista(size_t i) {
    const Chat* c = chat_de(mensajes[i].chat);
    armar_vista_core(mensajes[i], i > 0 ? &mensajes[i - 1] : nullptr, c && c->es_grupo,
                     [this](const std::string& j) { return nombre_de(j); }, w_conv(), vistas[i]);
}

void App::armar_vista_core(const Mensaje& m, const Mensaje* anterior, bool es_grupo,
                           const std::function<std::wstring(const std::string&)>& nombre_de, float W, VistaMensaje& v) {
    v = VistaMensaje();
    v.ancho_para = W;
    float tope = std::round(W * (W < 760 ? 0.84f : 0.68f) / 10) * 10;
    tope = std::min(tope, 720.0f);
    float interior = tope - 2 * PAD_X;

    v.hora_texto = formatear_hora(m.ts);
    if (m.editado) v.hora_texto = L"Edited  " + v.hora_texto;
    float hora_w = g.medir(v.hora_texto, HORA_TAM) + (m.propio ? 20.0f : 0.0f);

    v.divisor = !anterior || dia_de(m.ts) != dia_de(anterior->ts);
    if (v.divisor) v.divisor_texto = formatear_dia(m.ts);
    v.nuevo_bloque = !anterior || v.divisor || anterior->remitente != m.remitente || anterior->propio != m.propio;

    float ancho_contenido = 0, y = PAD_Y;
    if (es_grupo && !m.propio && v.nuevo_bloque) {
        v.nombre = g.texto(nombre_de(m.remitente), letra_chat - 1.5f, interior, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        DWRITE_TEXT_METRICS tm;
        v.nombre->GetMetrics(&tm);
        v.nh = tm.height;
        ancho_contenido = std::max(ancho_contenido, tm.widthIncludingTrailingWhitespace);
        y += v.nh + 2;
    }
    if (m.reenviado && !m.borrado) {
        y += 16;
        ancho_contenido = std::max(ancho_contenido, 80.0f);
    }
    if (!m.cita_id.empty()) {
        std::wstring autor = m.cita_remitente.empty() ? L"" : nombre_de(m.cita_remitente);
        std::wstring ct = m.cita_texto.empty() ? L"Message" : m.cita_texto;
        size_t corte = ct.find(L'\n');
        if (corte != std::wstring::npos) ct = ct.substr(0, corte) + L"…";
        std::wstring todo = autor + L"\n" + ct;
        v.cita = g.texto(todo, letra_chat - 1.5f, interior - 20);
        DWRITE_TEXT_RANGE rango = {0, (UINT32)autor.size()};
        v.cita->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, rango);
        v.cita->SetMaxHeight(60);
        DWRITE_TRIMMING recorte = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        v.cita->SetTrimming(&recorte, nullptr);
        DWRITE_TEXT_METRICS tm;
        v.cita->GetMetrics(&tm);
        v.ch = std::min(tm.height, 60.0f) + 10;
        ancho_contenido = std::max(ancho_contenido, std::min(tm.widthIncludingTrailingWhitespace + 22, interior));
        y += v.ch + 6;
    }
    bool media_visual = m.media && con_imagen(m.tipo) && !m.borrado;
    if (media_visual) {
        if (m.tipo == "figurita") {
            v.mw = v.mh = 160;
        } else {
            v.mw = std::min(interior, 340.0f);
            float rel = (m.media->ancho > 0 && m.media->alto > 0) ? (float)m.media->alto / m.media->ancho : 0.75f;
            v.mh = std::clamp(v.mw * rel, 60.0f, 460.0f);
        }
        v.mx = PAD_X;
        v.my = y;
        ancho_contenido = std::max(ancho_contenido, v.mw);
        y += v.mh + (m.texto.empty() ? 0 : 6);
    } else if (m.media && !m.borrado) {
        v.mw = std::min(interior, 280.0f);
        v.mh = (m.tipo == "audio" || m.tipo == "nota") ? 58.0f : 54.0f;
        v.mx = PAD_X;
        v.my = y;
        ancho_contenido = std::max(ancho_contenido, v.mw);
        // Sin texto, la hora va debajo de la tarjeta, no encima.
        y += v.mh + (m.texto.empty() ? 16 : 6);
    }

    std::wstring t = m.texto;
    if (m.borrado) t = L"\U0001F6AB This message was deleted";
    else if (t.empty() && !m.media) t = nombre_tipo(m.tipo);
    if (!t.empty()) {
        // Solo emojis: grandes (1 = 3x, 2 o 3 = 2x), como WhatsApp.
        float tam = letra_chat;
        if (!m.borrado && !m.media) {
            int n = solo_emojis(t);
            if (n == 1) tam = letra_chat * 3;
            else if (n == 2 || n == 3) tam = letra_chat * 2;
        }
        v.texto = g.texto(t, tam, interior);
        // Links: subrayados y en celeste.
        if (!m.borrado) {
            v.enlaces = buscar_enlaces(t);
            for (auto& e : v.enlaces) {
                DWRITE_TEXT_RANGE rango = {(UINT32)e.inicio, (UINT32)e.largo};
                v.texto->SetUnderline(TRUE, rango);
                v.texto->SetDrawingEffect(g.pincel_enlace.Get(), rango);
            }
        }
        DWRITE_TEXT_METRICS tm;
        v.texto->GetMetrics(&tm);
        v.tw = tm.widthIncludingTrailingWhitespace;
        v.th = tm.height;
        float fx = 0, fy = 0;
        DWRITE_HIT_TEST_METRICS hm;
        v.texto->HitTestTextPosition((UINT32)t.size(), FALSE, &fx, &fy, &hm);
        if (fx + 10 + hora_w <= interior) {
            v.hora_abajo = false;
            ancho_contenido = std::max(ancho_contenido, std::max(v.tw, fx + 10 + hora_w));
        } else {
            v.hora_abajo = true;
            ancho_contenido = std::max(ancho_contenido, v.tw);
            v.th += 15;
        }
        ancho_contenido = std::max(ancho_contenido, hora_w);
        v.ty = y;
        y += v.th;
    } else {
        ancho_contenido = std::max(ancho_contenido, hora_w + 8);
    }
    v.bw = ancho_contenido + 2 * PAD_X;
    v.bh = y + PAD_Y;
    if (v.texto) {
        v.hora_x = v.bw - PAD_X - hora_w;
        v.hora_y = v.ty + v.th - 15;
    } else {
        v.hora_x = v.bw - PAD_X - hora_w;
        v.hora_y = v.bh - PAD_Y - 15;
    }
    v.bx = m.propio ? W - 14 - v.bw : 14;
    v.by = (v.divisor ? 40.0f : 0.0f) + (v.nuevo_bloque ? 6.0f : 2.0f);
    v.reacciones_alto = m.reacciones.empty() ? 0.0f : 18.0f;
    v.alto = v.by + v.bh + v.reacciones_alto;
}

double App::alto_contenido() const {
    return (inicio.size() == vistas.size() + 1 ? inicio.back() : 0.0) + 24 + (alguien_escribe(chat_actual) ? ESCRIBIENDO_H : 0);
}

bool App::alguien_escribe(const std::string& chat) const {
    auto it = escribiendo.find(chat);
    return it != escribiendo.end() && it->second.hasta > GetTickCount64();
}

// La burbuja del que escribe: tres puntos que laten, o el microfono si graba.
void App::dibujar_burbuja_escribiendo(float y) {
    // Como un mensaje nuevo de otro bloque: 8 px de aire arriba.
    y += 8;
    float x = x_conv() + 14, bw = 62, bh = ESCRIBIENDO_H - 14;
    auto it = escribiendo.find(chat_actual);
    if (it != escribiendo.end() && it->second.grabando) {
        // Como WhatsApp: una burbuja redonda con el microfono titilando.
        float r = bh / 2, cx = x + r, cy = y + r;
        g.circulo(cx, cy, r, Color(BUBBLE_OTRA()));
        float f = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(ahora / 250.0f));
        g.renglon_fuente(L"Segoe MDL2 Assets", L"", cx - 8, cy - 8, 16, Color(TXT(), f));
        return;
    }
    g.rect_redondo(x, y, bw, bh, 8, Color(BUBBLE_OTRA()));
    for (int k = 0; k < 3; k++) {
        float f = 0.5f + 0.5f * std::sin((ahora / 180.0f) - k * 1.1f);
        g.circulo(x + 16 + k * 15, y + bh / 2, 4.5f, Color(TXT_DIM(), 0.35f + 0.65f * f));
    }
}

void App::recalcular_inicios() {
    inicio.resize(vistas.size() + 1);
    double acum = 0;
    for (size_t i = 0; i < vistas.size(); i++) {
        inicio[i] = acum;
        acum += vistas[i].alto;
    }
    inicio[vistas.size()] = acum;
}

bool App::al_final() const { return conv.objetivo >= conv.max - 4; }

void App::bajar_al_final(bool ya) {
    float H = g.alto - alto_cabecera() - alto_pie;
    recalcular_inicios();
    conv.max = std::max(0.0, alto_contenido() - H);
    conv.ir(conv.max, ya);
}

// Copia local de las imagenes (fotos, miniaturas, avatares): en
// datos\media, con el nombre de la clave. Lo que esta ahi no se vuelve a
// pedir al server nunca.
static std::wstring ruta_local_de(const std::string& clave) {
    std::string n = clave;
    for (auto& c : n)
        if (c == ':' || c == '@' || c == '/' || c == '?' || c == '=') c = '_';
    return carpeta_exe() + L"\\datos\\media\\" + ancho(n) + L".bin";
}

static std::string leer_bytes(const std::wstring& ruta) {
    std::string s;
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    LARGE_INTEGER tam;
    GetFileSizeEx(h, &tam);
    s.resize((size_t)tam.QuadPart);
    DWORD leido = 0;
    size_t total = 0;
    while (total < s.size() && ReadFile(h, s.data() + total, (DWORD)std::min<size_t>(1 << 20, s.size() - total), &leido, nullptr) && leido > 0)
        total += leido;
    CloseHandle(h);
    s.resize(total);
    return s;
}

static void escribir_bytes(const std::wstring& ruta, const std::string& datos) {
    CreateDirectoryW((carpeta_exe() + L"\\datos").c_str(), nullptr);
    CreateDirectoryW((carpeta_exe() + L"\\datos\\media").c_str(), nullptr);
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD escrito = 0;
    size_t total = 0;
    while (total < datos.size() && WriteFile(h, datos.data() + total, (DWORD)std::min<size_t>(1 << 20, datos.size() - total), &escrito, nullptr))
        total += escrito;
    CloseHandle(h);
}

Imagen& App::imagen(const std::string& clave, const std::wstring& ruta, bool animado) {
    Imagen& im = imagenes[clave];
    if (!im.pedida) {
        im.pedida = true;
        red::en_fondo([this, clave, ruta, animado] {
            // Primero el disco; si no esta, el server, y se guarda.
            // Los avatares no se guardan (cambian y son chicos).
            bool guardable = clave.rfind("foto:", 0) != 0;
            std::wstring local = ruta_local_de(clave);
            std::string datos = guardable ? leer_bytes(local) : std::string();
            if (datos.empty()) {
                Respuesta r = red::obtener(ruta, 120000);
                if (r.ok()) {
                    datos = r.cuerpo;
                    if (guardable && !datos.empty()) escribir_bytes(local, datos);
                }
            }
            Pixeles p;
            if (!datos.empty()) p = g.decodificar(datos, animado);
            red::en_ui([this, clave, p] {
                Imagen& x = imagenes[clave];
                if (p.vacio()) {
                    x.fallo = true;
                } else if (!p.cuadros.empty()) {
                    x.anim = p;
                } else {
                    x.bmp = g.subir(p);
                }
                pedir_dibujo();
            });
        });
    }
    return im;
}

// ---- dibujo ---------------------------------------------------------------

void App::dibujar() {
    ahora = GetTickCount64();
    float dt = ultimo_frame ? std::min((ahora - ultimo_frame) / 1000.0f, 0.05f) : 0.016f;
    ultimo_frame = ahora;
    lista.animar(dt);
    conv.animar(dt);
    scroll_res_chat.animar(dt);
    necesita_dibujar = false;

    g.empezar_frame();
    g.ctx->Clear(Color(BG_APP()).d2d());
    aplicar_ajustes();
    alto_pie = campo.alto(g) + 16;
    if (respondiendo || editando) alto_pie += BARRA_H;
    if (adjunto) alto_pie += ADJUNTO_H;
    dibujar_lista();
    dibujar_conversacion();
    dibujar_cabecera();
    dibujar_barra_llamada();
    dibujar_pie();
    dibujar_progreso_carga();
    dibujar_seleccion_barra();
    dibujar_info();
    dibujar_emojis();
    dibujar_visor();
    dibujar_modal_reenvio();
    dibujar_menu();
    g.terminar_frame();
}

// Arma los renglones de la lista: todos los chats, o los resultados de la
// busqueda (contactos que matchean + mensajes del server).
void App::armar_items() {
    items.clear();
    std::wstring q = plano(buscador.texto);
    while (!q.empty() && q.back() == L' ') q.pop_back();
    if (q.empty()) {
        for (size_t i = 0; i < chats.size(); i++) items.push_back({ItemLista::ChatItem, (int)i, L""});
        return;
    }
    items.push_back({ItemLista::Titulo, 0, L"Contacts"});
    for (size_t i = 0; i < chats.size(); i++)
        if (plano(chats[i].nombre).find(q) != std::wstring::npos) items.push_back({ItemLista::ChatItem, (int)i, L""});
    items.push_back({ItemLista::Titulo, 0, buscando ? L"Messages (searching...)" : L"Messages (" + std::to_wstring(resultados.size()) + L")"});
    for (size_t i = 0; i < resultados.size(); i++) items.push_back({ItemLista::Resultado, (int)i, L""});
}

int App::item_en(float y) {
    float yy = (float)(top_lista() - lista.pos);
    for (size_t i = 0; i < items.size(); i++) {
        float h = items[i].tipo == ItemLista::Titulo ? TITULO_H : fila_h();
        if (y >= yy && y < yy + h) return (int)i;
        yy += h;
    }
    return -1;
}

void App::dibujar_lista() {
    float W = ancho_lista, top = top_lista();
    g.rect(0, 0, W, g.alto, Color(BG_APP()));
    // Cabecera de la lista: titulo, aviso y buscador.
    g.rect(0, 0, W, top, Color(BG_PANEL()));
    g.renglon(reenviando ? L"Forward to..." : L"Chats", 16, 17, 20, Color(reenviando ? ACCENT() : TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    // El engranaje de settings, arriba a la derecha de la lista.
    g.renglon(L"⚙", W - 42, 16, 22, Color(TXT_DIM()));
    if (!aviso_estado.empty()) g.renglon(aviso_estado, 90, 22, 12, Color(0xf15c6d), DWRITE_FONT_WEIGHT_NORMAL, W - 100);
    g.rect_redondo(12, 60, W - 24, 34, 8, Color(BG_CAMPO()));
    g.lupa(29, 76, 6, Color(TXT_DIM()));
    buscador.dibujar(g, 40, 61, W - 54, 32, ahora);

    armar_items();
    float H = g.alto - top;
    float total = 0;
    for (auto& it : items) total += it.tipo == ItemLista::Titulo ? TITULO_H : fila_h();
    lista.max = std::max(0.0, (double)total - H);
    lista.limitar();
    if (!resultados.empty() && !buscando && !busqueda_completa && lista.pos > lista.max - H) buscar_mas();
    g.recortar(0, top, W, H);
    float y = (float)(top - lista.pos);
    for (size_t k = 0; k < items.size(); k++) {
        const ItemLista& it = items[k];
        float FILA_H = fila_h();
        float h = it.tipo == ItemLista::Titulo ? TITULO_H : FILA_H;
        if (y + h < top) {
            y += h;
            continue;
        }
        if (y > g.alto) break;
        if (it.tipo == ItemLista::Titulo) {
            g.renglon(it.titulo, 16, y + 10, 13, Color(ACCENT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            y += h;
            continue;
        }
        if (it.tipo == ItemLista::Resultado) {
            const Mensaje& m = resultados[it.idx];
            if ((int)k == chat_bajo_mouse) g.rect(0, y, W, FILA_H, Color(BG_HOVER()));
            std::wstring quien = nombre_de(m.chat);
            std::wstring fecha = formatear_dia(m.ts);
            float fw = g.medir(fecha, 12);
            g.renglon(fecha, W - 16 - fw, y + FILA_H * 0.2f, 12, Color(TXT_DIM()));
            g.renglon(quien, 16, y + FILA_H * 0.17f, letra_lista, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - 40 - fw);
            std::wstring prev = una_linea(m.texto);
            if (m.propio) prev = L"You: " + prev;
            g.renglon(prev, 16, y + FILA_H * 0.53f, letra_lista - 2, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - 32);
            g.linea(16, y + FILA_H - 0.5f, W, y + FILA_H - 0.5f, Color(BORDE()));
            y += h;
            continue;
        }
        const Chat& c = chats[it.idx];
        bool sel = c.jid == chat_actual;
        if (sel) g.rect(0, y, W, FILA_H, Color(BG_SEL()));
        else if ((int)k == chat_bajo_mouse) g.rect(0, y, W, FILA_H, Color(BG_HOVER()));
        // Avatar
        float r = (FILA_H - 20) / 2, cx = 16 + r, cy = y + FILA_H / 2;
        Imagen* foto = nullptr;
        if (c.tiene_foto || (!c.es_grupo && contactos.count(c.jid) && contactos[c.jid].tiene_foto))
            foto = &imagen("foto:" + c.jid, L"/foto/" + ancho(c.jid), false);
        if (foto && foto->bmp) {
            g.bitmap_circular(foto->bmp.Get(), cx, cy, r);
        } else {
            g.circulo(cx, cy, r, Color(0x6b7c85));
            std::wstring inicial = c.nombre.empty() ? L"?" : c.nombre.substr(0, 1);
            float iw = g.medir(inicial, r * 0.85f);
            g.renglon(inicial, cx - iw / 2, cy - r * 0.55f, r * 0.85f, Color(0xdfe5e7));
        }
        // Nombre y hora
        std::wstring hora = c.ultimo_ts ? formatear_hora(c.ultimo_ts) : L"";
        if (c.ultimo_ts && dia_de(c.ultimo_ts) != dia_de(ahora_ms())) hora = formatear_dia(c.ultimo_ts);
        float hw = g.medir(hora, 12);
        g.renglon(hora, W - 16 - hw, y + FILA_H * 0.2f, 12, Color(c.no_leidos ? ACCENT() : TXT_DIM()));
        float tx = 16 + 2 * r + 14;
        g.renglon(c.nombre, tx, y + FILA_H * 0.17f, letra_lista, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - tx - 24 - hw);
        // Ultimo mensaje (o el borrador, si hay)
        std::wstring prev;
        auto bd = borradores.find(c.jid);
        if (!sel && bd != borradores.end() && !bd->second.empty()) {
            prev = L"Draft: " + una_linea(bd->second);
        } else if (c.ultimo) {
            const Mensaje& u = *c.ultimo;
            if (u.borrado) prev = L"\U0001F6AB This message was deleted";
            else if (u.texto.empty()) prev = nombre_tipo(u.tipo);
            else if (con_imagen(u.tipo) || u.media) prev = nombre_tipo(u.tipo) + L" " + u.texto;
            else prev = u.texto;
            prev = una_linea(prev);
            if (u.propio) prev = L"     " + prev;  // lugar para los tildes
            else if (c.es_grupo) prev = nombre_de(u.remitente) + L": " + prev;
        }
        float ancho_prev = W - tx - 16;
        if (c.no_leidos) ancho_prev -= 34;
        float py = y + FILA_H * 0.53f;
        std::wstring escribe = texto_escribiendo(c.jid);
        if (!escribe.empty()) {
            g.renglon(escribe, tx, py, letra_lista - 2, Color(ACCENT()), DWRITE_FONT_WEIGHT_NORMAL, ancho_prev);
        } else {
        g.renglon(prev, tx, py, letra_lista - 2, Color(prev.rfind(L"Draft:", 0) == 0 ? 0xf15c6d : TXT_DIM()),
                  DWRITE_FONT_WEIGHT_NORMAL, ancho_prev);
        }
        if (escribe.empty() && c.ultimo && c.ultimo->propio && prev.rfind(L"Draft:", 0) != 0) {
            int estado = c.jid == mi_jid ? std::max(c.ultimo->estado, 2) : c.ultimo->estado;
            tildes(tx, py + 3, estado >= 2, Color(estado >= 3 ? TICK_AZUL() : TXT_DIM()));
        }
        if (c.no_leidos) {
            std::wstring n = std::to_wstring(c.no_leidos);
            float nw = g.medir(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            float pw = std::max(20.0f, nw + 12);
            g.rect_redondo(W - 16 - pw, py + 2, pw, 20, 10, Color(ACCENT()));
            g.renglon(n, W - 16 - pw + (pw - nw) / 2, py + 4, 11, Color(0x111b21), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
        g.linea(tx, y + FILA_H - 0.5f, W, y + FILA_H - 0.5f, Color(BORDE()));
        y += h;
    }
    g.destapar();
    barra_scroll(lista, W - 10, top, H, total, mouse_x > W - 24 && mouse_x < W && mouse_y > top, arrastrando_lista);
    g.rect(W - 1, 0, 1, g.alto, Color(BORDE()));
}

// La barra de scroll de un area: pulgar proporcional, mas visible con el
// mouse cerca o arrastrando.
void App::barra_scroll(const Desplazable& d, float x, float top, float H, double total, bool cerca, bool arrastrando) {
    if (d.max <= 0 || total <= H) return;
    float bh = std::max(30.0f, (float)(H * H / total));
    float by = top + (H - bh) * (float)(d.pos / d.max);
    g.rect_redondo(x, by, 6, bh, 3, Color(0xffffff, cerca || arrastrando ? 0.35f : 0.18f));
}

// Empieza a arrastrar el pulgar de una barra (o salta ahi si se clickeo la pista).
void App::agarrar_barra(Desplazable& d, float y, float top, float H, double total) {
    float bh = std::max(30.0f, (float)(H * H / total));
    float by = top + (H - bh) * (float)(d.pos / d.max);
    arrastre_origen = (y >= by && y <= by + bh) ? y - by : bh / 2;
}

void App::arrastrar_barra(Desplazable& d, float y, float top, float H, double total) {
    float bh = std::max(30.0f, (float)(H * H / total));
    double frac = (y - top - arrastre_origen) / (H - bh);
    d.ir(frac * d.max, true);
}

// Los tildes como los dibuja WhatsApp: dos "checks" superpuestos.
void App::tildes(float x, float y, bool doble, Color c) {
    auto check = [&](float ox) {
        g.linea(x + ox, y + 5.5f, x + ox + 3, y + 8.5f, c, 1.4f);
        g.linea(x + ox + 3, y + 8.5f, x + ox + 8.5f, y + 2.5f, c, 1.4f);
    };
    check(0);
    if (doble) check(4.5f);
}

void App::dibujar_cabecera() {
    float x = x_conv(), W = w_conv(), H = CABECERA_H;
    if (chat_actual.empty()) return;
    g.rect(x, 0, W, H, Color(BG_PANEL()));
    const Chat* c = chat_de(chat_actual);
    if (!c) return;
    float r = 20, cx = x + 16 + r, cy = H / 2;
    Imagen* foto = nullptr;
    if (c->tiene_foto || (contactos.count(c->jid) && contactos[c->jid].tiene_foto))
        foto = &imagen("foto:" + c->jid, L"/foto/" + ancho(c->jid), false);
    if (foto && foto->bmp) g.bitmap_circular(foto->bmp.Get(), cx, cy, r);
    else g.circulo(cx, cy, r, Color(0x6b7c85));
    if (busca_chat_abierta) {
        dibujar_busqueda_cabecera(cx + r + 14, x, W, H);
        return;
    }
    g.renglon(c->nombre, cx + r + 14, 12, 16, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - 200);
    std::wstring sub = texto_escribiendo(c->jid);
    bool escribe = !sub.empty();
    if (!escribe) sub = c->es_grupo ? L"Group" : formatear_telefono(c->jid);
    g.renglon(sub, cx + r + 14, 34, 12.5f, Color(escribe ? ACCENT() : TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - 200);
    // La lupa para buscar en este chat, "cargar todo" y los botones de llamar.
    if (!seleccionando) g.lupa(x + W - 72, 27, 8, Color(TXT_DIM()));
    if (!seleccionando) {
        // Siempre visible; apagado cuando no hay nada mas que traer.
        bool activo = ajustes::actual().mensajes_por_chat > 0 && hay_mas_viejos && !cargando_todo;
        g.renglon_fuente(L"Segoe MDL2 Assets", L"\uE896", x + W - 45, 21, 18, Color(TXT_DIM(), activo ? 1.0f : 0.35f));
    }
    dibujar_botones_llamada();
}

void App::dibujar_pie() {
    float x = x_conv(), W = w_conv(), H = alto_pie, y = g.alto - H;
    if (chat_actual.empty()) return;
    g.rect(x, y, W, H, Color(BG_PANEL()));
    float yy = y + 8;
    // Barra de respuesta / edicion.
    if (respondiendo || editando) {
        const Mensaje& m = respondiendo ? *respondiendo : *editando;
        Color color = editando ? Color(ACCENT()) : (m.propio ? Color(ACCENT()) : color_de_nombre(nombre_de(m.remitente)));
        g.rect_redondo(x + 16, yy, W - 32, BARRA_H - 8, 6, Color(BG_CAMPO()));
        g.rect_redondo(x + 16, yy, 4, BARRA_H - 8, 2, color);
        std::wstring titulo = editando ? L"Edit message" : (m.propio ? L"You" : nombre_de(m.remitente));
        g.renglon(titulo, x + 30, yy + 5, 13, color, DWRITE_FONT_WEIGHT_SEMI_BOLD, W - 100);
        std::wstring t = m.texto.empty() ? nombre_tipo(m.tipo) : una_linea(m.texto);
        g.renglon(t, x + 30, yy + 22, 12.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, W - 100);
        g.renglon(L"✕", x + W - 42, yy + 9, 16, Color(TXT_DIM()));
        yy += BARRA_H;
    }
    // Vista previa del adjunto.
    if (adjunto) {
        g.rect_redondo(x + 16, yy, W - 32, ADJUNTO_H - 8, 6, Color(BG_CAMPO()));
        if (adjunto->vista) {
            float esc = std::min((ADJUNTO_H - 24) / adjunto->h, 200.0f / adjunto->w);
            float w = adjunto->w * esc, h = adjunto->h * esc;
            g.recortar_redondo(x + 24, yy + 8, w, h, 4);
            g.bitmap(adjunto->vista.Get(), x + 24, yy + 8, w, h);
            g.destapar_redondo();
            g.renglon(ancho(adjunto->nombre), x + 24 + w + 14, yy + 12, 13, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - w - 120);
        } else {
            g.renglon(L"\U0001F4C4", x + 28, yy + 14, 28, Color(TXT_DIM()));
            g.renglon(ancho(adjunto->nombre), x + 72, yy + 14, 14, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, W - 160);
            g.renglon(std::to_wstring(adjunto->datos.size() / 1024) + L" KB", x + 72, yy + 36, 12, Color(TXT_DIM()));
        }
        if (adjunto->vista) {
            // Mandar como sticker en vez de foto.
            bool sticker = adjunto->tipo == "figurita";
            float sx = x + W - 180, sy = yy + ADJUNTO_H - 32;
            g.borde_redondo(sx, sy, 16, 16, 3, Color(sticker ? ACCENT() : TXT_DIM()), 1.5f);
            if (sticker) g.rect_redondo(sx + 3, sy + 3, 10, 10, 2, Color(ACCENT()));
            g.renglon(L"Send as sticker", sx + 24, sy - 1, 12.5f, Color(TXT()));
        }
        g.renglon(L"✕", x + W - 42, yy + 9, 16, Color(TXT_DIM()));
        yy += ADJUNTO_H;
    }
    float ch = campo.alto(g);
    if (grab != Grab::Nada) {
        dibujar_grabacion(x, yy, W, ch);
        return;
    }
    // Emojis y clip a la izquierda, el campo, y el boton de mandar/grabar.
    g.renglon(L"\U0001F642", x + 14, yy + ch / 2 - 12, 20, Color(emojis_abierto ? ACCENT() : TXT_DIM()));
    g.renglon(L"\U0001F4CE", x + 48, yy + ch / 2 - 12, 20, Color(TXT_DIM()));
    g.rect_redondo(x + 82, yy, W - 82 - 60, ch, 8, Color(BG_CAMPO()));
    campo.dibujar(g, x + 82, yy, W - 82 - 60, ch, ahora);
    bool hay_texto = !campo.texto.empty() || adjunto;
    g.renglon(hay_texto ? L"➤" : L"\U0001F3A4", x + W - 44, yy + ch / 2 - 12, 22, Color(hay_texto ? ACCENT() : TXT_DIM()));
}

// Lo que se ve sin ningun chat abierto (toda la columna derecha).
// Cada 2,5 s mientras no hay sesion: estado (ya escanearon?) y el QR nuevo.
void App::tic_vinculacion() {
    if (!sin_sesion) return;
    red::en_fondo([this] {
        Respuesta est = red::obtener(L"/estado", 8000);
        if (!est.ok()) return;
        Json je = Json::parsear(est.cuerpo);
        bool logueado = je["logueado"].bul();
        bool vinculando = je["vinculando"].bul();
        std::string png;
        if (!logueado) {
            if (!vinculando) red::mandar_json(L"/vincular", "{}");
            Respuesta q = red::obtener(L"/qr", 8000);
            if (q.ok()) png = q.cuerpo;
        }
        red::en_ui([this, logueado, png] {
            if (logueado) {
                // Listo: chats y todo lo demas.
                sin_sesion = false;
                qr_bmp.Reset();
                qr_datos.clear();
                KillTimer(hwnd, 10);
                cargar_chats();
            } else if (png != qr_datos) {
                qr_datos = png;
                qr_bmp.Reset();
                if (!png.empty()) {
                    Pixeles p = g.decodificar(png, false);
                    if (!p.vacio()) qr_bmp = g.subir(p);
                }
            }
            pedir_dibujo();
        });
    });
}

void App::dibujar_vinculacion() {
    float x = x_conv(), W = w_conv(), H = g.alto;
    g.rect(x, 0, W, H, Color(BG_SEL()));
    g.rect(x, 0, W, 6, Color(ACCENT()));
    float cx = x + W / 2;
    float lado = 264, bloque = 40 + 16 + lado + 16 + 24 + 3 * 24;
    float y = std::max(30.0f, (H - bloque) / 2);
    std::wstring t = L"Link your phone";
    float tw = g.medir(t, 26, DWRITE_FONT_WEIGHT_LIGHT);
    g.renglon(t, cx - tw / 2, y, 26, Color(TXT()), DWRITE_FONT_WEIGHT_LIGHT);
    y += 56;
    g.rect_redondo(cx - lado / 2 - 8, y - 8, lado + 16, lado + 16, 8, Color(0xffffff));
    if (qr_bmp) g.bitmap(qr_bmp.Get(), cx - lado / 2, y, lado, lado);
    else {
        std::wstring e = L"Getting the QR code...";
        float ew = g.medir(e, 14);
        g.renglon(e, cx - ew / 2, y + lado / 2 - 10, 14, Color(0x54656f));
    }
    y += lado + 32;
    const wchar_t* pasos[] = {L"1. Open WhatsApp on your phone",
                              L"2. Settings \u2192 Linked devices \u2192 Link a device",
                              L"3. Point your phone at this screen to scan the code"};
    for (const wchar_t* p : pasos) {
        float pw = g.medir(p, 14);
        g.renglon(p, cx - pw / 2, y, 14, Color(TXT_DIM()));
        y += 24;
    }
}

// Trae el chat entero (lo que falte de la cache y despues del server, de a
// 20k) mostrando el progreso. Solo tiene sentido con preload parcial.
void App::cargar_todo_el_chat() {
    if (chat_actual.empty() || cargando_todo || cargando_mensajes || !hay_mas_viejos) return;
    cargando_todo = true;
    cargando_mensajes = true;
    todo_cargado = (long long)mensajes.size();
    todo_total = 0;
    pedir_dibujo();
    std::string mio = chat_actual;
    long long mas_viejo = mensajes.empty() ? 0 : mensajes.front().ts;
    red::en_fondo([this, mio, mas_viejo] {
        long long viejo = mas_viejo;
        Respuesta rc = red::obtener(L"/cantidad?chat=" + ancho(mio), 30000);
        long long total = Json::parsear(rc.cuerpo)["cantidad"].entero();
        red::en_ui([this, mio, total] {
            if (mio == chat_actual) todo_total = std::max(total, todo_cargado);
            pedir_dibujo();
        });
        // Primero lo que ya tiene la cache.
        std::vector<Mensaje> resto = viejo > 0 ? cache::leer_mensajes(mio, viejo, 1000000) : std::vector<Mensaje>();
        if (!resto.empty()) {
            viejo = resto.front().ts;
            red::en_ui([this, mio, resto] {
                if (mio != chat_actual) return;
                anteponer(resto);
                todo_cargado += (long long)resto.size();
                pedir_dibujo();
            });
        }
        bool agotado = false, fallo = false;
        for (;;) {
            std::wstring url = L"/mensajes?chat=" + ancho(mio) + L"&limite=20000";
            if (viejo > 0) url += L"&antes=" + std::to_wstring(viejo);
            Respuesta r = red::obtener(url, 300000);
            if (!r.ok()) {
                fallo = true;
                break;
            }
            std::vector<Mensaje> viejos;
            Json j = Json::parsear(r.cuerpo);
            for (size_t i = 0; i < j.largo(); i++) viejos.push_back(Mensaje::de_json(j[i]));
            for (size_t i = 0; i < viejos.size(); i += 500)
                cache::guardar_mensajes(std::vector<Mensaje>(viejos.begin() + i, viejos.begin() + std::min(viejos.size(), i + 500)));
            agotado = viejos.size() < 20000;
            if (viejos.empty()) break;
            viejo = viejos.front().ts;
            bool seguir = true;
            red::en_ui([this, mio, viejos] {
                if (mio != chat_actual) return;
                anteponer(viejos);
                todo_cargado += (long long)viejos.size();
                pedir_dibujo();
            });
            (void)seguir;
            if (agotado) break;
        }
        red::en_ui([this, mio, agotado, fallo] {
            if (mio == chat_actual) {
                cargando_mensajes = false;
                hay_mas_viejos = fallo ? true : !agotado;
                if (fallo) aviso_estado = L"Cannot reach the server";
            }
            cargando_todo = false;
            pedir_dibujo();
        });
    });
}

// La ventanita de progreso, centrada sobre la conversacion.
void App::dibujar_progreso_carga() {
    if (!cargando_todo) return;
    float W = 360, H = 110, x = x_conv() + (w_conv() - W) / 2, y = alto_cabecera() + (g.alto - alto_pie - alto_cabecera() - H) / 2;
    g.rect_redondo(x, y, W, H, 12, Color(BG_PANEL()));
    g.borde_redondo(x, y, W, H, 12, Color(BORDE()), 1.0f);
    g.renglon(L"Loading all messages", x + 20, y + 16, 15, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto miles = [](long long n) {
        std::wstring s = std::to_wstring(n), o;
        int c = 0;
        for (int i = (int)s.size() - 1; i >= 0; i--) {
            o.insert(o.begin(), s[i]);
            if (++c % 3 == 0 && i > 0) o.insert(o.begin(), L'.');
        }
        return o;
    };
    std::wstring t = miles(todo_cargado) + (todo_total > 0 ? L" / " + miles(todo_total) : L"");
    float tw = g.medir(t, 13);
    g.renglon(t, x + W - 20 - tw, y + 18, 13, Color(TXT_DIM()));
    float bx = x + 20, by = y + 56, bw = W - 40, bh = 10;
    g.rect_redondo(bx, by, bw, bh, 5, Color(BG_CAMPO()));
    float f = todo_total > 0 ? std::clamp((float)todo_cargado / (float)todo_total, 0.0f, 1.0f) : 0.0f;
    if (f > 0) g.rect_redondo(bx, by, bw * f, bh, 5, Color(ACCENT()));
    int pct = (int)(f * 100 + 0.5f);
    std::wstring p = todo_total > 0 ? std::to_wstring(pct) + L"%" : L"Counting...";
    float pw = g.medir(p, 12);
    g.renglon(p, x + (W - pw) / 2, y + 78, 12, Color(TXT_DIM()));
}

void App::dibujar_pantalla_vacia() {
    float x = x_conv(), W = w_conv(), H = g.alto;
    g.rect(x, 0, W, H, Color(BG_SEL()));
    // La franja de arriba, marca registrada del panel vacio de WhatsApp.
    g.rect(x, 0, W, 6, Color(ACCENT()));
    float cx = x + W / 2;
    // Bloque centrado: circulo (190) + titulo + dos renglones.
    float bloque = 190 + 22 + 10 + 40 + 10 + 40;
    float y = std::max(30.0f, (H - 60 - bloque) / 2);
    g.circulo(cx, y + 95, 95, Color(0x2d3a42));
    std::wstring ic = L"\uE8BD";  // Segoe MDL2 "Message"
    float iw = g.medir_fuente(L"Segoe MDL2 Assets", ic, 88);
    g.renglon_fuente(L"Segoe MDL2 Assets", ic, cx - iw / 2, y + 95 - 44, 88, Color(0x3c4c55));
    y += 190 + 22;
    std::wstring t = L"kciwapp for Windows";
    float tw = g.medir(t, 30);
    g.renglon(t, cx - tw / 2, y, 30, Color(0xe9edef, 0.85f));
    y += 50;
    t = L"Pick a chat from the list to start messaging.";
    tw = g.medir(t, 14);
    g.renglon(t, cx - tw / 2, y, 14, Color(TXT_DIM()));
    y += 22;
    t = L"Your messages stay in sync with your phone.";
    tw = g.medir(t, 14);
    g.renglon(t, cx - tw / 2, y, 14, Color(TXT_DIM()));
    // Abajo: candadito + texto.
    t = L"End-to-end encrypted";
    tw = g.medir(t, 13);
    float lw = g.medir_fuente(L"Segoe MDL2 Assets", L"\uE72E", 13);
    float x0 = cx - (lw + 6 + tw) / 2, yb = H - 30 - 9;
    g.renglon_fuente(L"Segoe MDL2 Assets", L"\uE72E", x0, yb + 1, 13, Color(TXT_DIM()));
    g.renglon(t, x0 + lw + 6, yb, 13, Color(TXT_DIM()));
}

void App::dibujar_conversacion() {
    float x = x_conv(), W = w_conv(), top = alto_cabecera(), bottom = g.alto - alto_pie, H = bottom - top;
    dibujar_fondo_chat(x, top, W, H);
    if (sin_sesion) {
        dibujar_vinculacion();
        return;
    }
    if (chat_actual.empty()) {
        dibujar_pantalla_vacia();
        return;
    }
    if (panel_resultados_chat()) {
        dibujar_resultados_chat();
        return;
    }
    if (cargando_mensajes && mensajes.empty()) {
        std::wstring t = L"Loading...";
        float tw = g.medir(t, 15);
        g.renglon(t, x + (W - tw) / 2, top + H / 2 - 10, 15, Color(TXT_DIM()));
        return;
    }
    if (!cargando_mensajes && mensajes.empty()) {
        std::wstring t = L"No messages yet";
        float tw = g.medir(t, 15);
        g.renglon(t, x + (W - tw) / 2, top + H / 2 - 10, 15, Color(TXT_DIM()));
        return;
    }
    // Las vistas armadas con otro ancho se rearman.
    // Cambio de ancho: se rearma recien cuando el resize se aquieta (150 ms),
    // mientras tanto se dibuja con los layouts viejos.
    bool rearmar = false;
    for (size_t k = layout_pendiente; k < vistas.size(); k++)
        if (vistas[k].ancho_para != W) { rearmar = true; break; }
    if (rearmar) {
        if (ancho_pendiente != W) {
            ancho_pendiente = W;
            ultimo_resize = ahora;
            SetTimer(hwnd, 4, 170, nullptr);
        }
        if (ahora - ultimo_resize >= 150) {
            ancho_pendiente = 0;
            bool abajo = al_final();
            armar_vistas();
            recalcular_inicios();
            conv.max = std::max(0.0, alto_contenido() - H);
            if (abajo) conv.ir(conv.max, true);
        } else {
            // Mientras se arrastra, solo lo que esta en pantalla se rearma al
            // ancho nuevo (son ~20 layouts por frame); el resto espera.
            bool abajo = al_final();
            recalcular_inicios();
            double desde = conv.pos - 12;
            size_t i0 = std::upper_bound(inicio.begin(), inicio.end() - 1, desde) - inicio.begin();
            if (i0 > 0) i0--;
            double acum = 0;
            for (size_t i = i0; i < vistas.size() && acum < H + 200; i++) {
                if (vistas[i].ancho_para != W && vistas[i].alto > 0) armar_vista(i);
                acum += vistas[i].alto;
            }
            recalcular_inicios();
            conv.max = std::max(0.0, alto_contenido() - H);
            if (abajo) conv.ir(conv.max, true);
            pedir_dibujo();
        }
    }
    recalcular_inicios();
    conv.max = std::max(0.0, alto_contenido() - H);
    conv.limitar();
    if (conv.pos < 600 && hay_mas_viejos && !cargando_mensajes && layout_pendiente == 0) cargar_mas_viejos();

    if (traza_frames > 0) {
        traza_frames--;
        char buf[300];
        snprintf(buf, sizeof buf, "frame pos=%.2f obj=%.2f max=%.2f contenido=%.2f H=%.2f pie=%.2f pendiente=%zu n=%zu ultimo_alto=%.1f",
                 conv.pos, conv.objetivo, conv.max, alto_contenido(), (double)H, (double)alto_pie, layout_pendiente, vistas.size(),
                 vistas.empty() ? 0.0 : (double)vistas.back().alto);
        red::registrar(buf);
        pedir_dibujo();
    }
    g.recortar(x, top, W, H);
    // El primer mensaje que asoma, por busqueda binaria sobre los inicios.
    double desde = conv.pos - 12;
    size_t i0 = std::upper_bound(inicio.begin(), inicio.end() - 1, desde) - inicio.begin();
    if (i0 > 0) i0--;
    for (size_t i = i0; i < mensajes.size(); i++) {
        const VistaMensaje& v = vistas[i];
        if (v.alto <= 0) continue;
        float y = y_de(i);
        if (y + v.alto < top) continue;
        if (y > bottom) break;
        dibujar_mensaje(i, y);
    }
    if (alguien_escribe(chat_actual) && inicio.size() == vistas.size() + 1) {
        float y = y_de(vistas.size());
        if (y < bottom && y + ESCRIBIENDO_H > top) dibujar_burbuja_escribiendo(y);
    }
    if (!seleccionando && msg_bajo_mouse >= 0 && msg_bajo_mouse < (int)vistas.size()) dibujar_reacciones_de(msg_bajo_mouse, y_de((size_t)msg_bajo_mouse));
    if (reaccion_msg >= 0 && reaccion_msg != msg_bajo_mouse && reaccion_msg < (int)vistas.size())
        dibujar_reacciones_de(reaccion_msg, y_de((size_t)reaccion_msg));
    barra_scroll(conv, x + W - 10, top, H, alto_contenido(),
                 mouse_x > x + W - 24 && mouse_x < x + W && mouse_y > top && mouse_y < bottom, arrastrando_barra);
    // Boton para ir al final, cuando estamos lejos de el.
    if (conv.max - conv.objetivo > 150) {
        float cx = x + W - 44, cy = bottom - 36;
        g.circulo(cx, cy, 20, Color(BG_PANEL()));
        g.borde_redondo(cx - 20, cy - 20, 40, 40, 20, Color(BORDE()), 1.0f);
        Color c(TXT_DIM());
        g.linea(cx - 6, cy - 3, cx, cy + 3, c, 1.8f);
        g.linea(cx, cy + 3, cx + 6, cy - 3, c, 1.8f);
        const Chat* ch = chat_de(chat_actual);
        if (ch && ch->no_leidos > 0) {
            std::wstring n = std::to_wstring(ch->no_leidos);
            float nw = g.medir(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            float pw = std::max(20.0f, nw + 12);
            g.rect_redondo(cx + 8 - pw / 2, cy - 32, pw, 20, 10, Color(ACCENT()));
            g.renglon(n, cx + 8 - nw / 2, cy - 30, 11, Color(0x111b21), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
    }
    g.destapar();
}

void App::dibujar_mensaje(size_t i, float y) {
    const Mensaje& m = mensajes[i];
    const VistaMensaje& v = vistas[i];
    float x = x_conv(), W = w_conv();
    if (v.divisor) {
        float tw = g.medir(v.divisor_texto, 12);
        float px = x + (W - tw - 20) / 2;
        g.rect_redondo(px, y + 8, tw + 20, 24, 7, Color(DIVISOR()));
        g.renglon(v.divisor_texto, px + 10, y + 12, 12, Color(TXT_DIM()));
    }
    float bx = x + v.bx, by = y + v.by;
    if (seleccionando) {
        // Renglon resaltado si esta marcado, y el tilde a la izquierda.
        bool marcado = seleccionados.count(m.id) > 0;
        if (marcado) g.rect(x, y + v.by - 2, W, v.bh + 4, Color(ACCENT(), 0.12f));
        float cx = x + 18, cy = by + v.bh / 2;
        g.circulo(cx, cy, 10, Color(marcado ? ACCENT() : BG_CAMPO()));
        if (!marcado) g.borde_redondo(cx - 10, cy - 10, 20, 20, 10, Color(TXT_DIM()), 1.0f);
        else tildes(cx - 4.25f, cy - 5.5f, false, Color(0x111b21));
        if (!m.propio) bx += 26;
    }
    Color fondo(m.propio ? BUBBLE_MIA() : BUBBLE_OTRA());
    bool figurita = m.tipo == "figurita" && !m.borrado;
    if (!figurita) g.rect_redondo(bx, by, v.bw, v.bh, 8, fondo);
    // Resaltado al llegar desde una busqueda o notificacion: se apaga solo.
    if (m.id == resaltado_id) {
        if (busca_chat_abierta && elegido_chat >= 0) {
            // Elegido desde el buscador del chat: queda marcado hasta cerrarlo.
            g.rect_redondo(bx, by, v.bw, v.bh, 8, Color(ACCENT(), 0.30f));
            g.borde_redondo(bx + 0.5f, by + 0.5f, v.bw - 1, v.bh - 1, 8, Color(ACCENT()), 1.0f);
        } else {
            float t = (ahora - resaltado_desde) / 2000.0f;
            if (t < 1) g.rect_redondo(bx - 4, by - 4, v.bw + 8, v.bh + 8, 10, Color(ACCENT(), 0.35f * (1 - t)));
        }
    }
    float cy = by + PAD_Y, cx = bx + PAD_X;
    if (v.nombre) {
        g.dibujar_texto(v.nombre.Get(), cx, cy, color_de_nombre(nombre_de(m.remitente)));
        cy += v.nh + 2;
    }
    if (m.reenviado && !m.borrado) {
        g.renglon(L"↪ Forwarded", cx, cy, 11.5f, Color(TXT_DIM()));
        cy += 16;
    }
    if (v.cita) {
        float ancho_cita = v.bw - 2 * PAD_X;
        Color color = m.cita_remitente == mi_jid ? Color(ACCENT()) : color_de_nombre(nombre_de(m.cita_remitente));
        g.rect_redondo(cx, cy, ancho_cita, v.ch, 5, Color(0x000000, 0.22f));
        g.rect_redondo(cx, cy, 4, v.ch, 2, color);
        g.recortar(cx, cy, ancho_cita, v.ch);
        g.dibujar_texto(v.cita.Get(), cx + 12, cy + 5, Color(TXT_DIM()));
        g.destapar();
        cy += v.ch + 6;
    }
    if (v.mw > 0) {
        cx = bx + v.mx;
        cy = by + v.my;
        if (con_imagen(m.tipo) && m.media) {
            std::string clave_mini = "mini:" + std::to_string(m.media->id);
            std::string clave_media = "media:" + std::to_string(m.media->id);
            bool animado = m.tipo == "figurita" || m.tipo == "gif";
            ID2D1Bitmap1* b = nullptr;
            bool es_video = m.tipo == "video" || m.tipo == "gif";
            if (!es_video || figurita) {
                Imagen& im = imagen(clave_media, L"/media/" + std::to_wstring(m.media->id), animado);
                b = im.cuadro_actual(g, ahora);
                if (!im.anim.cuadros.empty()) necesita_dibujar = true;
            }
            if (!b && m.media->miniatura) {
                Imagen& mini = imagen(clave_mini, L"/miniatura/" + std::to_wstring(m.media->id), false);
                b = mini.bmp.Get();
            }
            if (figurita) {
                g.bitmap(b, cx, cy, v.mw, v.mh);
            } else {
                g.recortar_redondo(cx, cy, v.mw, v.mh, 6);
                if (b) {
                    // Cubrir el marco sin deformar.
                    D2D1_SIZE_F t = b->GetSize();
                    float esc = std::max(v.mw / t.width, v.mh / t.height);
                    float dw = t.width * esc, dh = t.height * esc;
                    g.bitmap(b, cx + (v.mw - dw) / 2, cy + (v.mh - dh) / 2, dw, dh);
                } else {
                    g.rect(cx, cy, v.mw, v.mh, Color(0x000000, 0.2f));
                    // Vencido en WhatsApp: se le pidio al telefono, se espera.
                    if (m.media->estado == 2 || imagenes["media:" + std::to_string(m.media->id)].fallo) {
                        std::wstring t = L"Expired – click to request from phone";
                        float tw = g.medir(t, 12);
                        g.renglon(t, cx + (v.mw - tw) / 2, cy + v.mh / 2 - 8, 12, Color(TXT_DIM()));
                    }
                }
                g.destapar_redondo();
                if (es_video) {
                    g.circulo(cx + v.mw / 2, cy + v.mh / 2, 24, Color(0x000000, 0.55f));
                    g.renglon(L"▶", cx + v.mw / 2 - 8, cy + v.mh / 2 - 12, 18, Color(0xffffff));
                }
            }
        } else if (m.media) {
            bool audio = m.tipo == "audio" || m.tipo == "nota";
            if (audio) {
                dibujar_audio((int)i, cx, cy, v.mw, v.mh);
            } else {
                // Documento: nombre y tamano.
                g.rect_redondo(cx, cy, v.mw, v.mh, 6, Color(0x000000, 0.18f));
                g.circulo(cx + 22, cy + v.mh / 2, 16, Color(ACCENT()));
                g.renglon(L"\U0001F4C4", cx + 15, cy + v.mh / 2 - 9, 14, Color(0x111b21));
                g.renglon(ancho(m.media->nombre), cx + 48, cy + 10, 13.5f, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, v.mw - 60);
                std::wstring tam = std::to_wstring(m.media->bytes / 1024) + L" KB";
                g.renglon(tam, cx + 48, cy + 30, 11, Color(TXT_DIM()));
            }
        }
    }
    if (v.texto) {
        float tx = bx + PAD_X, ty = by + v.ty;
        if (sel_msg == (int)i && sel_a != sel_b) {
            size_t a = std::min(sel_a, sel_b), b = std::max(sel_a, sel_b);
            UINT32 cuantos = 0;
            v.texto->HitTestTextRange((UINT32)a, (UINT32)(b - a), tx, ty, nullptr, 0, &cuantos);
            std::vector<DWRITE_HIT_TEST_METRICS> cajas(cuantos);
            v.texto->HitTestTextRange((UINT32)a, (UINT32)(b - a), tx, ty, cajas.data(), cuantos, &cuantos);
            for (auto& c : cajas) g.rect(c.left, c.top, c.width, c.height, Color(0x53bdeb, 0.4f));
        }
        g.dibujar_texto(v.texto.Get(), tx, ty, Color(m.borrado ? TXT_DIM() : TXT()));
    }
    // Hora y tildes
    float hx = bx + v.hora_x, hy = by + v.hora_y;
    bool sobre_media = !v.texto && v.mw > 0 && con_imagen(m.tipo);
    if (sobre_media) {
        float hw = g.medir(v.hora_texto, HORA_TAM) + (m.propio ? 20 : 0);
        g.rect_redondo(hx - 6, hy - 2, hw + 12, 18, 9, Color(0x000000, 0.45f));
    }
    float hw = g.renglon(v.hora_texto, hx, hy, HORA_TAM, Color(sobre_media ? 0xffffff : TXT_DIM()));
    if (m.propio) {
        int estado = m.chat == mi_jid ? std::max(m.estado, 2) : m.estado;
        Color c = estado >= 3 ? Color(TICK_AZUL()) : Color(sobre_media ? 0xffffff : TXT_DIM());
        if (estado == 0) {
            // Reloj: todavia no llego al servidor.
            g.ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(hx + hw + 10, hy + 7), 5, 5), g.pincel(c), 1.2f);
            g.linea(hx + hw + 10, hy + 4, hx + hw + 10, hy + 7.5f, c, 1.2f);
            g.linea(hx + hw + 10, hy + 7.5f, hx + hw + 12.5f, hy + 8.5f, c, 1.2f);
        } else {
            tildes(hx + hw + 4, hy + 2, estado >= 2, c);
        }
    }
    // Reacciones: una pastilla pisando el borde de abajo de la burbuja.
    if (!m.reacciones.empty()) {
        std::wstring t;
        std::vector<std::wstring> vistos;
        for (auto& r : m.reacciones)
            if (std::find(vistos.begin(), vistos.end(), r.emoji) == vistos.end()) {
                vistos.push_back(r.emoji);
                t += r.emoji;
            }
        if (m.reacciones.size() > 1) t += L" " + std::to_wstring(m.reacciones.size());
        float tw = g.medir(t, 12);
        float px = m.propio ? bx + v.bw - tw - 20 : bx + 4;
        g.rect_redondo(px, by + v.bh - 8, tw + 14, 22, 11, Color(BG_PANEL()));
        g.borde_redondo(px, by + v.bh - 8, tw + 14, 22, 11, Color(BG_CHAT()), 1.5f);
        g.renglon(t, px + 7, by + v.bh - 5, 12, Color(TXT()));
    }
}

// ---- entrada --------------------------------------------------------------

void App::raton_mueve(float x, float y) {
    mouse_x = x;
    mouse_y = y;
    int antes = chat_bajo_mouse;
    chat_bajo_mouse = x < ancho_lista && y > top_lista() ? item_en(y) : -1;
    if (antes != chat_bajo_mouse || emojis_abierto || info_abierto || menu_abierto) pedir_dibujo();
    cursor_mano = sobre_clickeable(x, y);
    cursor_texto = !cursor_mano && sobre_campo(x, y);
    {
        int antes_msg = msg_bajo_mouse;
        msg_bajo_mouse = -1;
        if (x >= ancho_lista && !visor && !info_abierto && y > alto_cabecera() && y < g.alto - alto_pie) {
            float ym = 0;
            int i = mensaje_en(y, &ym);
            // Tambien vale estar sobre la carita (que esta fuera de la burbuja).
            if (i < 0 && antes_msg >= 0 && antes_msg < (int)vistas.size()) {
                float yy = y_de((size_t)antes_msg);
                if (y >= yy + vistas[antes_msg].by - 4 && y <= yy + vistas[antes_msg].by + vistas[antes_msg].bh + 4) i = antes_msg;
            }
            msg_bajo_mouse = i;
        }
        if (antes_msg != msg_bajo_mouse) pedir_dibujo();
        if (reaccion_msg >= 0) pedir_dibujo();
    }
    if (arrastrando_barra) {
        float top = alto_cabecera(), H = g.alto - alto_pie - top;
        arrastrar_barra(conv, y, top, H, alto_contenido());
        pedir_dibujo();
    }
    if (arrastrando_lista) {
        float top = top_lista(), H = g.alto - top;
        arrastrar_barra(lista, y, top, H, lista.max + H);
        pedir_dibujo();
    }
    if (visor_vol && seek_w > 0) {
        int v = (int)std::round(std::clamp((x - seek_x) / seek_w, 0.0f, 1.0f) * 100);
        ajustes::cambiar([v](Ajustes& a) { a.volumen_video = v; });
        reproductor.volumen(v);
        pedir_dibujo();
    }
    if (visor_seek && seek_w > 0) {
        double d = reproductor.duracion();
        if (d > 0) reproductor.ir_a(d * std::clamp((x - seek_x) / seek_w, 0.0f, 1.0f));
        pedir_dibujo();
    }
    if (visor_arrastrando) {
        visor_px += x - visor_ax;
        visor_py += y - visor_ay;
        visor_mov += std::abs(x - visor_ax) + std::abs(y - visor_ay);
        visor_ax = x;
        visor_ay = y;
        pedir_dibujo();
    }
    if (seek_msg >= 0 && seek_w > 0) {
        // Arrastrando sobre la onda: se sigue el mouse.
        double d = reproductor.duracion();
        if (d > 0) reproductor.ir_a(d * std::clamp((x - seek_x) / seek_w, 0.0f, 1.0f));
        pedir_dibujo();
    }
    if (campo.arrastrando) {
        campo.arrastrar(g, x, y);
        pedir_dibujo();
    }
    if (buscador.arrastrando) {
        buscador.arrastrar(g, x, y);
        pedir_dibujo();
    }
    if (buscador_chat.arrastrando) {
        buscador_chat.arrastrar(g, x, y);
        pedir_dibujo();
    }
    if (arrastrando_res_chat) {
        float top = alto_cabecera(), H = g.alto - alto_pie - top;
        arrastrar_barra(scroll_res_chat, y, top, H, res_chat.size() * fila_h());
        pedir_dibujo();
    }
    if (panel_resultados_chat() && x >= ancho_lista) pedir_dibujo();  // hover de las filas
    if (sel_arrastrando && sel_msg >= 0 && sel_msg < (int)mensajes.size()) {
        float ym = 0;
        // El mensaje de la seleccion, este donde este ahora.
        if (inicio.size() != vistas.size() + 1) recalcular_inicios();
        ym = y_de((size_t)sel_msg);
        size_t idx = 0;
        en_texto(sel_msg, ym, x, y, &idx);
        sel_b = idx;
        pedir_dibujo();
    }
}

void App::raton_abajo(float x, float y, bool shift) {
    SetCapture(hwnd);
    if (click_menu(x, y)) return;
    if (visor) {
        click_visor(x, y);
        pedir_dibujo();
        return;
    }
    if (click_modal_reenvio(x, y)) {
        pedir_dibujo();
        return;
    }
    if (click_emojis(x, y)) {
        pedir_dibujo();
        return;
    }
    if (click_seleccion(x, y)) return;
    if (click_info(x, y)) {
        pedir_dibujo();
        return;
    }
    buscador.foco = false;
    emoji_buscador.foco = false;
    buscador_chat.foco = false;
    if (click_llamada(x, y)) {
        pedir_dibujo();
        return;
    }
    if (click_busqueda_chat(x, y, shift)) {
        pedir_dibujo();
        return;
    }
    if (x < ancho_lista) {
        if (y < 60 && x > ancho_lista - 56) {
            ventana_ajustes::abrir(hwnd);
            return;
        }
        if (y > top_lista() && lista.max > 0 && x > ancho_lista - 24) {
            float top = top_lista(), H = g.alto - top;
            arrastrando_lista = true;
            agarrar_barra(lista, y, top, H, lista.max + H);
            raton_mueve(x, y);
            return;
        }
        if (y >= 60 && y < top_lista()) {
            buscador.foco = true;
            campo.foco = false;
            buscador.click(g, x, y, shift);
            pedir_dibujo();
            return;
        }
        int k = item_en(y);
        if (k >= 0 && k < (int)items.size()) {
            const ItemLista& it = items[k];
            if (it.tipo == ItemLista::ChatItem) abrir_chat(chats[it.idx].jid);
            else if (it.tipo == ItemLista::Resultado) {
                const Mensaje& m = resultados[it.idx];
                ir_a_mensaje(m.chat, m.id, m.ts);
            }
        }
        return;
    }
    float top = alto_cabecera(), bottom = g.alto - alto_pie;
    float W = w_conv();
    if (y < top && !chat_actual.empty() && !seleccionando) {
        abrir_info(chat_actual);
        return;
    }
    // El boton de ir al final.
    if (conv.max - conv.objetivo > 150 && x > x_conv() + W - 68 && x < x_conv() + W - 20 && y > bottom - 60 && y < bottom - 12) {
        bajar_al_final(false);
        pedir_dibujo();
        return;
    }
    if (y >= bottom) {
        campo.foco = true;
        float yy = bottom + 8;
        if ((respondiendo || editando) && y < yy + BARRA_H) {
            if (x > x_conv() + W - 56) {
                if (editando) campo.poner(L"");
                respondiendo.reset();
                editando.reset();
            }
            pedir_dibujo();
            return;
        }
        if (respondiendo || editando) yy += BARRA_H;
        if (adjunto && y < yy + ADJUNTO_H) {
            if (x > x_conv() + W - 56 && y < yy + 40) adjunto.reset();
            else if (adjunto->vista && x > x_conv() + W - 180 && y > yy + ADJUNTO_H - 40)
                adjunto->tipo = adjunto->tipo == "figurita" ? "imagen" : "figurita";
            pedir_dibujo();
            return;
        }
        if (adjunto) yy += ADJUNTO_H;
        if (grab != Grab::Nada) {
            click_grabacion(x, y, yy, campo.alto(g));
            return;
        }
        if (x < x_conv() + 44) {
            emojis_abierto = !emojis_abierto;
            emoji_buscador.foco = false;
            pedir_dibujo();
            return;
        }
        if (x < x_conv() + 82) {
            elegir_archivo();
            return;
        }
        if (x > x_conv() + W - 60) {
            if (adjunto) enviar_adjunto();
            else if (!campo.texto.empty()) enviar_texto();
            else grabar_empezar();
            pedir_dibujo();
            return;
        }
        campo.click(g, x, y, shift);
        pedir_dibujo();
        return;
    }
    if (y > top && conv.max > 0 && x > g.ancho - 24) {
        float H = bottom - top;
        double total = alto_contenido();
        float bh = std::max(30.0f, (float)(H * H / total));
        float by = top + (H - bh) * (float)(conv.pos / conv.max);
        arrastrando_barra = true;
        arrastre_origen = (y >= by && y <= by + bh) ? y - by : bh / 2;
        raton_mueve(x, y);
        return;
    }
    // Click en la conversacion: reacciones, seleccion de texto, o abrir media.
    campo.foco = true;
    if (click_reacciones(x, y)) return;
    sel_msg = -1;
    float ym = 0;
    int i = mensaje_en(y, &ym);
    if (i >= 0) {
        size_t idx = 0;
        if (en_texto(i, ym, x, y, &idx)) {
            enlace_pendiente = enlace_en(i, ym, x, y);
            sel_msg = i;
            sel_a = sel_b = idx;
            sel_arrastrando = true;
            sel_x0 = x;
            sel_y0 = y;
        } else {
            const VistaMensaje& v = vistas[i];
            float mx = x_conv() + v.bx + v.mx, my = ym + v.by + v.my;
            if (v.mw > 0 && x >= mx && x <= mx + v.mw && y >= my && y <= my + v.mh) {
                if ((mensajes[i].tipo == "audio" || mensajes[i].tipo == "nota") && reproductor_ok)
                    click_audio(i, x - mx, y - my, v.mw, v.mh);
                else
                    abrir_media(i);
            }
        }
    }
    pedir_dibujo();
}

void App::raton_arriba(float, float) {
    ReleaseCapture();
    arrastrando_barra = false;
    arrastrando_lista = false;
    seek_msg = -1;
    visor_seek = false;
    visor_vol = false;
    visor_arrastrando = false;
    campo.arrastrando = false;
    buscador.arrastrando = false;
    buscador_chat.arrastrando = false;
    arrastrando_res_chat = false;
    if (sel_arrastrando) {
        sel_arrastrando = false;
        if (sel_a == sel_b) {
            sel_msg = -1;
            if (!enlace_pendiente.empty())
                ShellExecuteW(nullptr, L"open", enlace_pendiente.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        enlace_pendiente.clear();
        pedir_dibujo();
    }
}

void App::rueda(float x, float y, float delta) {
    if (visor) {
        rueda_visor(x, y, delta);
        return;
    }
    // delta en "muescas" (120 = una); tres renglones por muesca, como Windows.
    UINT lineas = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lineas, 0);
    float px = -delta / 120.0f * lineas * 40.0f;
    if (rueda_modal_reenvio(x, y, delta) || rueda_emojis(x, y, delta) || rueda_info(x, y, delta)) return;
    if (x < ancho_lista) lista.rodar(px);
    else if (panel_resultados_chat() && y > alto_cabecera() && y < g.alto - alto_pie) scroll_res_chat.rodar(px);
    else if (y > alto_cabecera() && y < g.alto - alto_pie) conv.rodar(px);
    pedir_dibujo();
}

void App::tecla(WPARAM vk, bool shift, bool ctrl) {
    if (vk == VK_F9) {
        // Prueba de notificacion.
        toast::mostrar(L"kciwapp test", L"If you see this, notifications work", chat_actual, "", L"");
        return;
    }
    if (vk == VK_F10) {
        webwa::volcar_dom();
        return;
    }
    if (vk == VK_F11) {
        traza_frames = 300;
        red::registrar("F11: traza de 300 frames");
        pedir_dibujo();
        return;
    }
    if (vk == VK_F12) {
        // Traza de depuracion del chat abierto.
        char buf[512];
        size_t sin_alto = 0;
        for (size_t k = layout_pendiente; k < vistas.size(); k++)
            if (vistas[k].alto <= 0) sin_alto++;
        snprintf(buf, sizeof buf,
                 "F12 chat=%s mensajes=%zu vistas=%zu pendiente=%zu sin_alto=%zu contenido=%.0f pos=%.0f obj=%.0f max=%.0f H=%.0f pie=%.0f ultimo_ts=%lld ultimo=%s",
                 chat_actual.c_str(), mensajes.size(), vistas.size(), layout_pendiente, sin_alto, alto_contenido(), conv.pos,
                 conv.objetivo, conv.max, (double)(g.alto - alto_cabecera() - alto_pie), (double)alto_pie,
                 mensajes.empty() ? 0LL : mensajes.back().ts,
                 mensajes.empty() ? "" : angosto(mensajes.back().texto.substr(0, 30)).c_str());
        red::registrar(buf);
        for (size_t k = vistas.size() > 5 ? vistas.size() - 5 : 0; k < vistas.size(); k++) {
            snprintf(buf, sizeof buf, "  v[%zu] alto=%.0f by=%.0f bh=%.0f ancho_para=%.0f ts=%lld", k, vistas[k].alto, vistas[k].by,
                     vistas[k].bh, vistas[k].ancho_para, mensajes[k].ts);
            red::registrar(buf);
        }
        return;
    }
    if (vk == VK_ESCAPE) {
        escapar();
        return;
    }
    if (ctrl && vk == 'F') {
        // Con un chat abierto se busca ahi; sin chat, en la lista.
        if (!chat_actual.empty() && !info_abierto) abrir_busqueda_chat();
        else {
            buscador.foco = true;
            campo.foco = false;
            buscador.seleccionar_todo();
        }
        pedir_dibujo();
        return;
    }
    if (busca_chat_abierta && !res_chat.empty()) {
        // Arriba/F3 = mas viejo, abajo/Shift+F3 = mas nuevo; Enter en el campo
        // con resultados ya elegidos tambien avanza.
        if (vk == VK_UP || (vk == VK_F3 && !shift)) { mover_coincidencia(+1); return; }
        if (vk == VK_DOWN || (vk == VK_F3 && shift)) { mover_coincidencia(-1); return; }
        if (vk == VK_RETURN && buscador_chat.foco && elegido_chat >= 0 && buscador_chat.texto == ultima_busqueda_chat) {
            mover_coincidencia(+1);
            return;
        }
    }
    if (buscador_chat.foco && buscador_chat.tecla(g, vk, shift, ctrl)) {
        pedir_dibujo();
        return;
    }
    if ((ctrl && vk == 'V') || (shift && vk == VK_INSERT)) {
        if (campo.foco) {
            pegar();
            return;
        }
    }
    if (ctrl && vk == VK_INSERT && sel_msg >= 0 && sel_a != sel_b && !campo.hay_seleccion()) {
        copiar_seleccion();
        return;
    }
    if (ctrl && vk == 'C' && sel_msg >= 0 && sel_a != sel_b && !campo.hay_seleccion()) {
        copiar_seleccion();
        return;
    }
    if (ctrl && (vk == VK_OEM_PLUS || vk == VK_ADD || vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) {
        bool mas = vk == VK_OEM_PLUS || vk == VK_ADD;
        ajustes::cambiar([&](Ajustes& a) {
            a.letra_chat = std::clamp(a.letra_chat + (mas ? 1.0f : -1.0f), 10.0f, 24.0f);
            a.letra_lista = std::clamp(a.letra_lista + (mas ? 1.0f : -1.0f), 10.0f, 24.0f);
        });
        pedir_dibujo();
        return;
    }
    if (modal_reenvio && buscador_reenvio.tecla(g, vk, shift, ctrl)) {
        scroll_reenvio.ir(0, true);
        pedir_dibujo();
        return;
    }
    if (emoji_buscador.foco && emoji_buscador.tecla(g, vk, shift, ctrl)) {
        emoji_scroll.ir(0, true);
        pedir_dibujo();
        return;
    }
    if (buscador.foco && buscador.tecla(g, vk, shift, ctrl)) {
        pedir_dibujo();
        return;
    }
    if (campo.foco && campo.tecla(g, vk, shift, ctrl)) {
        pedir_dibujo();
        return;
    }
    if (vk == VK_PRIOR) conv.rodar(-(g.alto - alto_cabecera() - alto_pie) * 0.9f);
    if (vk == VK_NEXT) conv.rodar((g.alto - alto_cabecera() - alto_pie) * 0.9f);
    pedir_dibujo();
}

void App::caracter(wchar_t c) {
    if (buscador_chat.foco) {
        if (buscador_chat.caracter(c)) pedir_dibujo();
        return;
    }
    if (buscador.foco) {
        if (buscador.caracter(c)) pedir_dibujo();
        return;
    }
    if (modal_reenvio) {
        if (buscador_reenvio.caracter(c)) {
            scroll_reenvio.ir(0, true);
            pedir_dibujo();
        }
        return;
    }
    if (emoji_buscador.foco) {
        if (emoji_buscador.caracter(c)) {
            emoji_scroll.ir(0, true);
            pedir_dibujo();
        }
        return;
    }
    if (campo.foco && campo.caracter(c)) pedir_dibujo();
}

// ---- ajustes en vivo ------------------------------------------------------

// Lo que cambio en la ventana de settings se aplica aca, una vez por cambio.
void App::aplicar_ajustes() {
    unsigned rev = ajustes::revision();
    if (rev == ajustes_aplicados) return;
    ajustes_aplicados = rev;
    const Ajustes& a = ajustes::actual();
    if (letra_chat != a.letra_chat || letra_lista != a.letra_lista) {
        letra_chat = a.letra_chat;
        letra_lista = a.letra_lista;
        campo.tamano = letra_chat + 0.5f;
        bool abajo = al_final();
        armar_vistas();
        if (abajo) bajar_al_final(true);
    }
    if (reproductor_ok) reproductor.salida(a.salida);
    if (a.llamadas_web && !webwa::activo()) webwa::iniciar(hwnd, carpeta_exe());
    else if (!a.llamadas_web && webwa::activo()) {
        webwa::cerrar();
        terminar_llamada_ui();
    }
    if (fondo_cargado != a.fondo) {
        fondo_cargado = a.fondo;
        fondo_bmp.Reset();
        fondo_pincel.Reset();
        std::wstring ruta;
        if (a.fondo == L"whatsapp") ruta = carpeta_exe() + L"\\fondo-wa.webp";
        else if (!a.fondo.empty()) ruta = ajustes::carpeta_fondos() + L"\\" + a.fondo;
        if (!ruta.empty()) {
            std::string datos;
            HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER tam;
                GetFileSizeEx(h, &tam);
                datos.resize((size_t)tam.QuadPart);
                DWORD leido = 0;
                ReadFile(h, datos.data(), (DWORD)datos.size(), &leido, nullptr);
                CloseHandle(h);
            }
            Pixeles p = g.decodificar(datos, false);
            if (!p.vacio()) {
                fondo_bmp = g.subir(p);
                if (a.fondo == L"whatsapp" && fondo_bmp) {
                    // Los garabatos se repiten como mosaico.
                    D2D1_BITMAP_BRUSH_PROPERTIES bp = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);
                    D2D1_BRUSH_PROPERTIES bp2 = D2D1::BrushProperties();
                    ComPtr<ID2D1BitmapBrush> pincel;
                    g.ctx->CreateBitmapBrush(fondo_bmp.Get(), &bp, &bp2, &pincel);
                    fondo_pincel = pincel;
                }
            }
        }
    }
    pedir_dibujo();
}

// El fondo de la conversacion: color liso, garabatos en mosaico o una foto.
void App::dibujar_fondo_chat(float x, float y, float w, float h) {
    g.rect(x, y, w, h, Color(BG_CHAT()));
    if (fondo_pincel) {
        bool claro = ajustes::paleta().claro;
        fondo_pincel->SetOpacity(claro ? 0.4f : 0.06f);
        fondo_pincel->SetTransform(D2D1::Matrix3x2F::Scale(0.5f, 0.5f) * D2D1::Matrix3x2F::Translation(x, y));
        g.ctx->FillRectangle(D2D1::RectF(x, y, x + w, y + h), fondo_pincel.Get());
    } else if (fondo_bmp) {
        D2D1_SIZE_F t = fondo_bmp->GetSize();
        float esc = std::max(w / t.width, h / t.height);
        float dw = t.width * esc, dh = t.height * esc;
        g.recortar(x, y, w, h);
        g.bitmap(fondo_bmp.Get(), x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
        g.destapar();
    }
}

// Que hay bajo el mouse que se pueda clickear: para poner la manito.
// Sobre algun campo de texto que se dibujo en el ultimo frame (y no esta
// tapado por el visor o el menu).
bool App::sobre_campo(float x, float y) const {
    if (visor || menu_abierto) return false;
    const Campo* campos[] = {&campo, &buscador, &buscador_chat, &buscador_reenvio, &emoji_buscador};
    for (const Campo* c : campos) {
        if (c->dibujado_en != ultimo_frame || !c->tiene(x, y)) continue;
        // Con el modal de reenvio abierto solo cuenta su buscador.
        if (modal_reenvio && c != &buscador_reenvio) continue;
        return true;
    }
    return false;
}

bool App::sobre_clickeable(float x, float y) {
    if (visor) {
        if (visor_video) return true;
        float ix, iy, iw, ih;
        return !(rect_visor(ix, iy, iw, ih) && x >= ix && x <= ix + iw && y >= iy && y <= iy + ih) ||
               (x > g.ancho - 50 && y < 50);
    }
    if (x < ancho_lista) {
        if (y < 60) return x > ancho_lista - 56;              // engranaje
        if (y < top_lista()) return false;                     // buscador
        int k = item_en(y);
        return k >= 0 && k < (int)items.size() && items[k].tipo != ItemLista::Titulo;
    }
    if (emojis_abierto) return true;
    if (info_abierto) return y < alto_cabecera() + 60 || y > alto_cabecera();
    float top = alto_cabecera(), bottom = g.alto - alto_pie, W = w_conv();
    if (clickeable_llamada(x, y)) return true;
    if (llamada_activa && y >= CABECERA_H && y < top) return false;  // la barra de la llamada
    if (clickeable_busqueda_chat(x, y)) return true;
    if (panel_resultados_chat() && y >= top && y < bottom) return false;
    if (y < top) return !chat_actual.empty() && !busca_chat_abierta;  // cabecera -> info
    if (y >= bottom) {
        if (chat_actual.empty()) return false;
        float yy = bottom + 8;
        if ((respondiendo || editando) && y < yy + BARRA_H) return x > x_conv() + W - 56;
        if (respondiendo || editando) yy += BARRA_H;
        if (adjunto && y < yy + ADJUNTO_H) return x > x_conv() + W - 56 || (x > x_conv() + W - 180 && y > yy + ADJUNTO_H - 40);
        if (adjunto) yy += ADJUNTO_H;
        if (grab != Grab::Nada) return x < x_conv() + 92 || x > x_conv() + W - 60;
        return x < x_conv() + 82 || x > x_conv() + W - 60;    // emoji, clip, mandar/mic
    }
    if (conv.max - conv.objetivo > 150 && x > x_conv() + W - 68 && x < x_conv() + W - 20 && y > bottom - 60 && y < bottom - 12)
        return true;                                           // ir al final
    if (conv.max > 0 && x > g.ancho - 24) return true;         // barra
    float ym = 0;
    int i = mensaje_en(y, &ym);
    if (i < 0) return false;
    if (!enlace_en(i, ym, x, y).empty()) return true;
    const VistaMensaje& v = vistas[i];
    float mx = x_conv() + v.bx + v.mx, my = ym + v.by + v.my;
    return v.mw > 0 && x >= mx && x <= mx + v.mw && y >= my && y <= my + v.mh;  // foto, video, audio, documento
}

// Trae del server los ultimos N del chat (segun Preload), pisa la cache y
// lo muestra. Para cuando la historia cambio del lado del server.
void App::refrescar_chat_del_server(const std::string& jid) {
    chats_para_refrescar.erase(jid);
    int cuantos = ajustes::actual().mensajes_por_chat;
    if (cuantos <= 0) cuantos = 1000000;
    cargando_mensajes = true;
    aviso_estado = L"Refreshing chat...";
    pedir_dibujo();
    std::string mio = jid;
    red::en_fondo([this, mio, cuantos] {
        Respuesta r = red::obtener(L"/mensajes?chat=" + ancho(mio) + L"&limite=" + std::to_wstring(cuantos), 600000);
        auto nuevos = std::make_shared<std::vector<Mensaje>>();
        Json j = Json::parsear(r.cuerpo);
        nuevos->reserve(j.largo());
        for (size_t i = 0; i < j.largo(); i++) nuevos->push_back(Mensaje::de_json(j[i]));
        bool ok = r.ok();
        if (ok)
            for (size_t i = 0; i < nuevos->size(); i += 500)
                cache::guardar_mensajes(std::vector<Mensaje>(nuevos->begin() + i, nuevos->begin() + std::min(nuevos->size(), i + 500)));
        red::en_ui([this, mio, nuevos, ok, cuantos] {
            aviso_estado.clear();
            if (mio != chat_actual) return;
            cargando_mensajes = false;
            if (!ok) {
                aviso_estado = L"Cannot reach the server";
                pedir_dibujo();
                return;
            }
            bool abajo = mensajes.empty() || al_final();
            mensajes = std::move(*nuevos);
            hay_mas_viejos = (int)mensajes.size() >= cuantos;
            armar_vistas();
            if (abajo) bajar_al_final(true);
            else {
                recalcular_inicios();
                conv.max = std::max(0.0, alto_contenido() - (g.alto - alto_cabecera() - alto_pie));
                conv.limitar();
            }
            pedir_dibujo();
        });
    });
}
