// Mensajes interactivos (bots de negocios): botones, listas, plantillas,
// productos. Se dibujan debajo del texto de la burbuja, de borde a borde,
// como en WhatsApp; el click responde por el server (POST /boton), abre el
// link, copia el telefono/codigo o despliega la lista en un menu propio.
#include "app.h"

#include <shellapi.h>
#include "red.h"
#include "tema.h"

std::shared_ptr<Botones> Botones::de_json(const Json& j) {
    auto b = std::make_shared<Botones>();
    b->tipo = j["tipo"].str();
    b->titulo = ancho(j["titulo"].str());
    b->pie = ancho(j["pie"].str());
    b->boton_lista = ancho(j["boton_lista"].str());
    b->respondido = ancho(j["respondido"].str());
    const Json& bs = j["botones"];
    for (size_t i = 0; i < bs.largo(); i++) {
        Boton x;
        x.id = bs[i]["id"].str();
        x.tipo = bs[i]["tipo"].str();
        x.texto = ancho(bs[i]["texto"].str());
        x.url = bs[i]["url"].str();
        x.telefono = bs[i]["telefono"].str();
        x.codigo = bs[i]["codigo"].str();
        b->botones.push_back(x);
    }
    const Json& ss = j["secciones"];
    for (size_t i = 0; i < ss.largo(); i++) {
        SeccionLista s;
        s.titulo = ancho(ss[i]["titulo"].str());
        const Json& fs = ss[i]["filas"];
        for (size_t k = 0; k < fs.largo(); k++)
            s.filas.push_back({fs[k]["id"].str(), ancho(fs[k]["titulo"].str()), ancho(fs[k]["descripcion"].str())});
        b->secciones.push_back(s);
    }
    return b;
}

// Cuantas filas de boton dibuja la burbuja (la lista es una sola).
int Botones::filas() const { return (int)botones.size() + (secciones.empty() ? 0 : 1); }

namespace {
constexpr float BOTON_H = 38.0f;
constexpr float PAD_X = 9.0f;  // el de las burbujas (app.cpp)

// Texto y glifo de la fila k (los botones sueltos primero, la lista al final).
void fila_de(const Botones& b, int k, std::wstring& texto, const wchar_t*& icono, const Boton*& boton) {
    boton = nullptr;
    if (k < (int)b.botones.size()) {
        boton = &b.botones[k];
        texto = boton->texto;
        icono = boton->tipo == "url" ? L"" : boton->tipo == "llamar" ? L"" : boton->tipo == "copiar" ? L"" : boton->tipo == "responder" ? L"" : nullptr;
    } else {
        texto = b.boton_lista.empty() ? L"Options" : b.boton_lista;
        icono = L"";
    }
}

void al_portapapeles(HWND hwnd, const std::wstring& t) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), t.c_str(), (t.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
}  // namespace

float App::alto_botones(const Mensaje& m) const {
    if (!m.botones || m.borrado) return 0;
    return (m.botones->pie.empty() ? 0.0f : 18.0f) + BOTON_H * m.botones->filas();
}

// Debajo del texto: el pie chiquito y las filas de boton, de borde a borde.
void App::dibujar_botones(const Mensaje& m, const VistaMensaje& v, float bx, float by) {
    const Botones& b = *m.botones;
    float y = by + v.botones_y;
    if (!b.pie.empty()) {
        g.renglon(b.pie, bx + PAD_X, y + 1, 11.5f, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, v.bw - 2 * PAD_X);
        y += 18;
    }
    bool ya = !b.respondido.empty();
    for (int k = 0; k < b.filas(); k++) {
        std::wstring texto;
        const wchar_t* icono = nullptr;
        const Boton* boton = nullptr;
        fila_de(b, k, texto, icono, boton);
        g.linea(bx, y + 0.5f, bx + v.bw, y + 0.5f, Color(BORDE()));
        bool elegido = ya && boton && boton->texto == b.respondido;
        bool apagado = ya && boton && boton->tipo == "responder" && !elegido;
        Color c = apagado ? Color(TXT_DIM(), 0.6f) : Color(ACCENT());
        float tw = g.medir(texto, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD), iw = icono ? 22.0f : 0.0f;
        float x0 = bx + (v.bw - tw - iw) / 2;
        if (icono) g.renglon_fuente(L"Segoe MDL2 Assets", icono, x0, y + 12, 14, c);
        g.renglon(texto, x0 + iw, y + 9, 14, c, DWRITE_FONT_WEIGHT_SEMI_BOLD, v.bw - 20);
        if (elegido) g.renglon_fuente(L"Segoe MDL2 Assets", L"", bx + v.bw - 26, y + 12, 13, c);
        y += BOTON_H;
    }
}

// Que fila de boton esta bajo (x, y); -1 si ninguna.
int App::boton_en(int i, float ym, float x, float y) const {
    if (i < 0 || i >= (int)vistas.size() || i >= (int)mensajes.size()) return -1;
    const Mensaje& m = mensajes[i];
    const VistaMensaje& v = vistas[i];
    if (!m.botones || v.botones_n == 0) return -1;
    float bx = x_conv() + v.bx, by = ym + v.by + v.botones_y + (m.botones->pie.empty() ? 0.0f : 18.0f);
    if (x < bx || x > bx + v.bw || y < by) return -1;
    int k = (int)((y - by) / BOTON_H);
    return k < v.botones_n ? k : -1;
}

void App::click_boton(int i, int k) {
    if (i < 0 || i >= (int)mensajes.size() || !mensajes[i].botones) return;
    Mensaje& m = mensajes[i];
    const Botones& b = *m.botones;
    std::wstring texto;
    const wchar_t* icono = nullptr;
    const Boton* boton = nullptr;
    fila_de(b, k, texto, icono, boton);
    if (!boton) {
        // La lista: un menu propio con las secciones y sus filas.
        std::vector<ItemMenu> items;
        std::vector<std::pair<std::string, std::wstring>> filas;
        for (auto& s : b.secciones) {
            if (!s.titulo.empty()) {
                ItemMenu t{s.titulo, 0};
                t.habilitado = false;
                items.push_back(t);
            }
            for (auto& f : s.filas) {
                filas.push_back({f.id, f.titulo});
                std::wstring t = f.titulo;
                if (!f.descripcion.empty()) t += L"  ·  " + f.descripcion;
                items.push_back({t, (int)filas.size(), L""});
            }
        }
        std::string chat = m.chat, id = m.id;
        abrir_menu(std::move(items), mouse_x, mouse_y, [this, chat, id, filas](int elegido) {
            if (elegido <= 0 || elegido > (int)filas.size()) return;
            responder_boton(chat, id, "", filas[elegido - 1].first, filas[elegido - 1].second);
        });
        return;
    }
    if (boton->tipo == "url") {
        if (!boton->url.empty()) ShellExecuteW(nullptr, L"open", ancho(boton->url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (boton->tipo == "llamar") {
        al_portapapeles(hwnd, ancho(boton->telefono));
        aviso_estado = L"Phone number copied: " + ancho(boton->telefono);
    } else if (boton->tipo == "copiar") {
        al_portapapeles(hwnd, ancho(boton->codigo));
        aviso_estado = L"Copied: " + ancho(boton->codigo);
    } else if (boton->tipo == "responder") {
        if (!b.respondido.empty()) return;  // ya se contesto
        responder_boton(m.chat, m.id, boton->id, "", boton->texto);
    }
    pedir_dibujo();
}

// Manda la respuesta al server; la anota como mensaje propio al volver.
void App::responder_boton(const std::string& chat, const std::string& id, const std::string& boton, const std::string& fila, const std::wstring& texto) {
    std::string cuerpo = "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"boton\":" + json_texto(boton) + ",\"fila\":" + json_texto(fila) + "}";
    aviso_estado = L"Sending...";
    pedir_dibujo();
    red::en_fondo([this, cuerpo, chat, id, texto] {
        Respuesta r = red::mandar_json(L"/boton", cuerpo);
        Json j = Json::parsear(r.cuerpo);
        bool ok = r.ok();
        red::en_ui([this, j, ok, chat, id, texto] {
            aviso_estado.clear();
            if (!ok) {
                aviso_estado = L"Could not send: " + ancho(j["error"].str("no response"));
                pedir_dibujo();
                return;
            }
            for (auto& m : mensajes)
                if (m.chat == chat && m.id == id && m.botones) m.botones->respondido = texto;
            if (chat == chat_actual) agregar_mensaje(Mensaje::de_json(j));
            pedir_dibujo();
        });
    });
}
