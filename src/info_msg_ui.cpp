// "Message info" de un mensaje mio, como en el telefono: quien lo leyo y
// cuando, a quien le llego, y quien falta (en grupos). Un modal propio sobre
// todo, con scroll. Los datos vienen de GET /acuses (y /miembros para saber
// quien falta); los eventos "acuse" lo van actualizando en vivo.
#include "app.h"
#include "json.h"
#include "red.h"
#include "tema.h"

namespace {

constexpr float FILA_H = 44.0f;
constexpr float TITULO_H = 30.0f;

struct GeoInfo {
    float x, y, w, h;   // la tarjeta
    float lx, ly, lw, lh;  // la lista (con scroll)
};

GeoInfo geo_info(float W, float H) {
    GeoInfo q;
    q.w = std::min(460.0f, W - 40);
    q.h = std::min(560.0f, H - 60);
    q.x = (W - q.w) / 2;
    q.y = (H - q.h) / 2;
    q.lx = q.x;
    q.ly = q.y + 56;
    q.lw = q.w;
    q.lh = q.h - 56 - 12;
    return q;
}

std::wstring cuando(long long ts) { return formatear_dia(ts) + L", " + formatear_hora(ts); }

}  // namespace

void App::abrir_info_mensaje(int i) {
    if (i < 0 || i >= (int)mensajes.size() || !mensajes[i].propio) return;
    info_msg_abierto = true;
    info_msg_chat = mensajes[i].chat;
    info_msg_id = mensajes[i].id;
    info_msg_ts = mensajes[i].ts;
    info_msg_acuses.clear();
    info_msg_miembros.clear();
    info_msg_cargando = true;
    info_msg_scroll.ir(0, true);
    info_msg_scroll.max = 0;
    info_msg_scroll.max = 0;
    campo.foco = false;
    pedir_dibujo();
    std::string chat = info_msg_chat, id = info_msg_id;
    const Chat* c = chat_de(chat);
    bool grupo = c && c->es_grupo;
    red::en_fondo([this, chat, id, grupo] {
        Respuesta r = red::obtener(L"/acuses?chat=" + ancho(chat) + L"&id=" + ancho(id), 15000);
        Json ja = Json::parsear(r.cuerpo);
        std::vector<Acuse> acuses;
        for (size_t k = 0; k < ja.largo(); k++) acuses.push_back({ja[k]["quien"].str(), (int)ja[k]["estado"].entero(), ja[k]["ts"].entero()});
        std::vector<std::string> miembros;
        if (grupo) {
            Respuesta rm = red::obtener(L"/miembros?chat=" + ancho(chat), 15000);
            Json jm = Json::parsear(rm.cuerpo);
            for (size_t k = 0; k < jm.largo(); k++) miembros.push_back(jm[k]["jid"].str());
        }
        red::en_ui([this, chat, id, acuses, miembros] {
            if (!info_msg_abierto || info_msg_chat != chat || info_msg_id != id) return;
            info_msg_acuses = acuses;
            info_msg_miembros = miembros;
            info_msg_cargando = false;
            pedir_dibujo();
        });
    });
}

void App::cerrar_info_mensaje() {
    info_msg_abierto = false;
    if (!config_pendiente && !selector_pendiente && !sin_sesion) campo.foco = true;
    pedir_dibujo();
}

// Un acuse nuevo (evento): si es de este mensaje, se refleja al instante.
void App::acuse_para_info(const std::string& chat, const std::string& id, const std::string& quien, int estado, long long ts) {
    if (!info_msg_abierto || chat != info_msg_chat || id != info_msg_id || quien.empty()) return;
    for (auto& a : info_msg_acuses)
        if (a.quien == quien) {
            if (estado > a.estado) {
                a.estado = estado;
                a.ts = ts;
            }
            pedir_dibujo();
            return;
        }
    info_msg_acuses.push_back({quien, estado, ts});
    pedir_dibujo();
}

// Las tres listas: leido (3, ordenado por hora), entregado (2), pendiente.
void App::armar_secciones_info(std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>>>& secciones) {
    std::vector<std::pair<std::wstring, std::wstring>> leidos, entregados, pendientes;
    std::vector<Acuse> orden = info_msg_acuses;
    std::sort(orden.begin(), orden.end(), [](const Acuse& a, const Acuse& b) { return a.ts > b.ts; });
    std::set<std::string> con_acuse;
    for (const Acuse& a : orden) {
        con_acuse.insert(a.quien);
        (a.estado >= 3 ? leidos : entregados).push_back({nombre_de(a.quien), cuando(a.ts)});
    }
    for (const std::string& j : info_msg_miembros)
        if (j != mi_jid && !con_acuse.count(j)) pendientes.push_back({nombre_de(j), L""});
    const Chat* c = chat_de(info_msg_chat);
    bool grupo = c && c->es_grupo;
    if (!grupo && info_msg_chat != "status@broadcast") {
        // Chat de a dos: "Read" / "Delivered" con la hora, sin nombres.
        for (auto& l : leidos) l.first = L"Read";
        for (auto& e : entregados) e.first = L"Delivered";
        if (leidos.empty() && entregados.empty()) pendientes.push_back({L"Not delivered yet", L""});
    }
    if (!leidos.empty()) secciones.push_back({(info_msg_chat == "status@broadcast" ? L"Seen by " : L"Read by ") + std::to_wstring(leidos.size()), leidos});
    if (!entregados.empty()) secciones.push_back({L"Delivered to " + std::to_wstring(entregados.size()), entregados});
    if (!pendientes.empty()) secciones.push_back({grupo ? L"Pending " + std::to_wstring(pendientes.size()) : L"", pendientes});
}

void App::dibujar_info_mensaje() {
    if (!info_msg_abierto) return;
    float W = g.ancho, H = g.alto;
    GeoInfo q = geo_info(W, H);
    g.rect(0, 0, W, H, Color(0x000000, 0.55f));
    g.rect_redondo(q.x, q.y, q.w, q.h, 12, Color(BG_PANEL()));
    g.borde_redondo(q.x, q.y, q.w, q.h, 12, Color(BORDE()), 1.0f);
    g.renglon(L"Message info", q.x + 20, q.y + 16, 16, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(L"✕", q.x + q.w - 36, q.y + 14, 16, Color(TXT_DIM()));
    g.renglon(L"Sent " + cuando(info_msg_ts), q.x + 20, q.y + 38, 12, Color(TXT_DIM()));
    std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>>> secciones;
    armar_secciones_info(secciones);
    float total = 0;
    for (auto& s : secciones) total += (s.first.empty() ? 0 : TITULO_H) + s.second.size() * FILA_H + 8;
    info_msg_scroll.max = std::max(0.0, (double)total - q.lh);
    info_msg_scroll.limitar();
    g.recortar(q.lx, q.ly, q.lw, q.lh);
    float y = q.ly - (float)info_msg_scroll.pos;
    if (info_msg_cargando) g.renglon(L"Loading...", q.x + 20, y + 8, 13, Color(TXT_DIM()));
    else if (secciones.empty()) g.renglon(L"No receipts yet.", q.x + 20, y + 8, 13, Color(TXT_DIM()));
    for (auto& s : secciones) {
        if (!s.first.empty()) {
            g.renglon(s.first, q.x + 20, y + 8, 12, Color(ACCENT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            y += TITULO_H;
        }
        for (auto& [nombre, hora] : s.second) {
            g.circulo(q.x + 38, y + FILA_H / 2, 16, color_de_nombre(nombre));
            std::wstring ini = nombre.substr(0, 1);
            float iw = g.medir(ini, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            g.renglon(ini, q.x + 38 - iw / 2, y + FILA_H / 2 - 9, 14, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            g.renglon(nombre, q.x + 66, y + 7, 14, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, q.w - 90);
            if (!hora.empty()) g.renglon(hora, q.x + 66, y + 26, 11.5f, Color(TXT_DIM()));
            y += FILA_H;
        }
        y += 8;
    }
    g.destapar();
}

bool App::click_info_mensaje(float x, float y) {
    if (!info_msg_abierto) return false;
    GeoInfo q = geo_info(g.ancho, g.alto);
    bool fuera = x < q.x || x >= q.x + q.w || y < q.y || y >= q.y + q.h;
    bool cruz = x >= q.x + q.w - 44 && x < q.x + q.w - 8 && y >= q.y + 8 && y < q.y + 40;
    if (fuera || cruz) cerrar_info_mensaje();
    return true;
}

bool App::sobre_info_mensaje(float x, float y) const {
    if (!info_msg_abierto) return false;
    GeoInfo q = geo_info(g.ancho, g.alto);
    return x >= q.x + q.w - 44 && x < q.x + q.w - 8 && y >= q.y + 8 && y < q.y + 40;
}

bool App::rueda_info_mensaje(float, float, float delta) {
    if (!info_msg_abierto) return false;
    info_msg_scroll.rodar(-delta / 120.0f * 60.0f);
    pedir_dibujo();
    return true;
}
