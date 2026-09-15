// Ventana de ajustes: propia, dueno = la principal (encima pero no modal),
// con su propio Gfx dibujando en el mismo hilo/loop de mensajes que main.cpp.
// Todo cambio se aplica en vivo con ajustes::cambiar(...); no hay boton Apply.
#include "ventana_ajustes.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <vector>

#include "gfx.h"
#include "grabador.h"
#include "tema.h"

namespace {

// ---------------------------------------------------------------------
// Geometria general.
constexpr float PADDING = 20.0f;
constexpr float FILA = 40.0f;
constexpr float FILA_LISTA = 34.0f;
constexpr float ESPACIO_SECCION = 22.0f;
constexpr float POPUP_W = 260.0f;
constexpr float POPUP_H = 312.0f;
constexpr float SV_LADO = 180.0f;
constexpr float BARRA_ANCHO = 18.0f;

// ---------------------------------------------------------------------
// Conversion de color: el estado del selector se guarda en HSL (h en
// grados 0..360, s y l en 0..1) y NUNCA se recalcula desde el RGB en cada
// arrastre. Si se recalculara, al llegar a blanco/negro/gris el matiz (h)
// y la saturacion se pierden (son indeterminados ahi) y la barra de matiz
// queda pegada/rota. El cuadrado SV se dibuja y se lee en HSV (con el
// matiz de turno), pero apenas se aplica un punto del cuadrado se
// convierte una sola vez a HSL y de ahi en mas se mueve ese HSL.

void rgb_de_hex(unsigned hex, float& r, float& g, float& b) {
    r = ((hex >> 16) & 255) / 255.0f;
    g = ((hex >> 8) & 255) / 255.0f;
    b = (hex & 255) / 255.0f;
}

unsigned hex_de_rgb(float r, float g, float b) {
    auto c = [](float v) -> unsigned { return (unsigned)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

struct Hsl { float h = 0, s = 0, l = 0; };

Hsl hsl_de_hex(unsigned hex) {
    float r, g, b;
    rgb_de_hex(hex, r, g, b);
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    float l = (mx + mn) / 2, s = 0, h = 0;
    float d = mx - mn;
    if (d > 0.00001f) {
        s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = fmodf((g - b) / d + (g < b ? 6.0f : 0.0f), 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h *= 60.0f;
    }
    return {h, s, l};
}

unsigned rgb_de_hsl(float h, float s, float l) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return hex_de_rgb(r + m, g + m, b + m);
}

unsigned rgb_de_hsv(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return hex_de_rgb(r + m, g + m, b + m);
}

// s y v (HSV) del color actual, solo para ubicar la marca en el cuadrado.
void hsv_de_hex(unsigned hex, float& s, float& v) {
    float r, g, b;
    rgb_de_hex(hex, r, g, b);
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    v = mx;
    s = mx <= 0.0001f ? 0.0f : (mx - mn) / mx;
}

// ---------------------------------------------------------------------
// Los 10 colores editables del tema custom (los que pide la seccion
// "Colors"). El resto de la Paleta (bg_hover, tick_azul, divisor, borde)
// no se expone: se derivan de estos o no hacen falta en el editor.
enum CampoColor {
    CC_FONDO, CC_PANELES, CC_FONDO_CHAT, CC_BURBUJA_MIA, CC_BURBUJA_OTRA,
    CC_TEXTO, CC_TEXTO_DIM, CC_ACENTO, CC_CAMPO, CC_SELECCION, CC_CANTIDAD,
};

const wchar_t* NOMBRE_CAMPO[CC_CANTIDAD] = {
    L"Background", L"Panels", L"Chat background", L"My bubbles", L"Their bubbles",
    L"Text", L"Dim text", L"Accent", L"Field", L"Selection",
};

unsigned leer_campo(const Paleta& p, int campo) {
    switch (campo) {
        case CC_FONDO: return p.bg_app;
        case CC_PANELES: return p.bg_panel;
        case CC_FONDO_CHAT: return p.bg_chat;
        case CC_BURBUJA_MIA: return p.burbuja_mia;
        case CC_BURBUJA_OTRA: return p.burbuja_otra;
        case CC_TEXTO: return p.txt;
        case CC_TEXTO_DIM: return p.txt_dim;
        case CC_ACENTO: return p.acento;
        case CC_CAMPO: return p.bg_campo;
        case CC_SELECCION: return p.bg_sel;
    }
    return 0;
}

void escribir_campo(Paleta& p, int campo, unsigned c) {
    switch (campo) {
        case CC_FONDO: p.bg_app = c; break;
        case CC_PANELES: p.bg_panel = c; break;
        case CC_FONDO_CHAT: p.bg_chat = c; break;
        case CC_BURBUJA_MIA: p.burbuja_mia = c; break;
        case CC_BURBUJA_OTRA: p.burbuja_otra = c; break;
        case CC_TEXTO: p.txt = c; break;
        case CC_TEXTO_DIM: p.txt_dim = c; break;
        case CC_ACENTO: p.acento = c; break;
        case CC_CAMPO: p.bg_campo = c; break;
        case CC_SELECCION: p.bg_sel = c; break;
    }
}

std::wstring wstr_de(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ---------------------------------------------------------------------
// Estado de la ventana (variables estaticas: una sola ventana de ajustes
// a la vez, como pide la tarea).
HWND g_hwnd = nullptr;
Gfx g_gfx;
float g_scroll = 0;
float g_contenido_alto = 0;
float g_mouse_x = 0, g_mouse_y = 0;

std::vector<grabador::Dispositivo> g_mics, g_altavoces;

struct EstadoPicker {
    bool abierto = false;
    int campo = -1;
    float h = 0, s = 0, l = 0;   // HSL de trabajo
    unsigned original = 0;       // color al abrir, para Cancel
    int arrastre = 0;            // 0 nada, 1 cuadrado, 2 matiz, 3 luminosidad
    float popup_x = 0, popup_y = 0;
};
EstadoPicker g_picker;

// Rects (en DIPs, ya en espacio de ventana) de los controles del selector,
// anotados al dibujar y usados al arrastrar.
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
Rect g_rect_cuadrado, g_rect_hue, g_rect_luz;

bool adentro(float px, float py, const Rect& r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}
bool adentro(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Regiones clickeables: se recalculan enteras en cada frame (dibujar_todo),
// porque la posicion de todo depende del scroll y de que secciones estan
// visibles. g_clics es la UI normal; g_clics_popup solo existe (y solo se
// prueba) mientras el selector de color esta abierto.
struct RegionClic { float x, y, w, h; std::function<void()> accion; };
std::vector<RegionClic> g_clics, g_clics_popup;

void agregar_clic(std::vector<RegionClic>& v, float x, float y, float w, float h, std::function<void()> accion) {
    v.push_back({x, y, w, h, std::move(accion)});
}

// ---------------------------------------------------------------------
// Helpers de dibujo.

float centrado(Gfx& g, const std::wstring& s, float cx, float y, float tam, Color c,
                DWRITE_FONT_WEIGHT peso = DWRITE_FONT_WEIGHT_NORMAL) {
    float mw = g.medir(s, tam, peso);
    return g.renglon(s, cx - mw / 2.0f, y, tam, c, peso);
}

float titulo_seccion(Gfx& g, float x, float y, const wchar_t* txt) {
    g.renglon(txt, x, y, 13, Color(ACCENT()), DWRITE_FONT_WEIGHT_BOLD);
    return y + 24;
}

// Sin resaltado al pasar el mouse: el usuario no lo quiere.
void hover_si(Gfx&, float, float, float, float) {}

// Anillo circular: un rect redondo con radio = mitad del lado dibuja un
// circulo perfecto de borde (truco para no tocar gfx.h/gfx.cpp).
void anillo(Gfx& g, float cx, float cy, float radio, Color c, float grosor = 2.0f) {
    g.borde_redondo(cx - radio, cy - radio, radio * 2, radio * 2, radio, c, grosor);
}

// ---------------------------------------------------------------------
// Selector de color: abrir/cancelar/aceptar y aplicar un arrastre.

unsigned color_actual_picker() { return rgb_de_hsl(g_picker.h, g_picker.s, g_picker.l); }

void aplicar_hsl(float h, float s, float l) {
    g_picker.h = h;
    g_picker.s = s;
    g_picker.l = l;
    unsigned c = rgb_de_hsl(h, s, l);
    if (g_picker.campo >= 0 && leer_campo(ajustes::actual().custom, g_picker.campo) != c)
        ajustes::cambiar([c](Ajustes& a) { escribir_campo(a.custom, g_picker.campo, c); });
}

void en_cuadrado(float x, float y) {
    if (g_rect_cuadrado.w <= 0) return;
    float s = std::clamp((x - g_rect_cuadrado.x) / g_rect_cuadrado.w, 0.0f, 1.0f);
    float v = std::clamp(1.0f - (y - g_rect_cuadrado.y) / g_rect_cuadrado.h, 0.0f, 1.0f);
    Hsl r = hsl_de_hex(rgb_de_hsv(g_picker.h, s, v));
    aplicar_hsl(g_picker.h, r.s, r.l);
}

void en_hue(float y) {
    if (g_rect_hue.h <= 0) return;
    float h = std::clamp((y - g_rect_hue.y) / g_rect_hue.h, 0.0f, 0.9999f) * 360.0f;
    float s = g_picker.s, l = g_picker.l;
    // Sobre un gris (blanco, negro o cualquier saturacion ~0) el matiz no
    // se ve: se le da saturacion entera y una luminosidad media si hace
    // falta, si no la barra parece no hacer nada (el bug conocido).
    if (s < 0.01f) {
        s = 1.0f;
        if (!(l > 0.1f && l < 0.9f)) l = 0.5f;
    }
    aplicar_hsl(h, s, l);
}

void en_luz(float y) {
    if (g_rect_luz.h <= 0) return;
    float l = std::clamp(1.0f - (y - g_rect_luz.y) / g_rect_luz.h, 0.0f, 1.0f);
    aplicar_hsl(g_picker.h, g_picker.s, l);
}

std::string g_tema_antes;  // el tema que habia al abrir el picker, para el Cancel

void abrir_picker(int campo, float swatch_x, float swatch_y, float swatch_w, float swatch_h) {
    g_picker.abierto = true;
    g_picker.campo = campo;
    g_picker.arrastre = 0;
    // Editar un color de un preset: el preset se copia a Custom y se pasa a Custom.
    g_tema_antes = ajustes::actual().tema;
    if (g_tema_antes != "custom") {
        Paleta base = ajustes::paleta();
        ajustes::cambiar([base](Ajustes& a) {
            a.custom = base;
            a.tema = "custom";
        });
    }
    unsigned c = leer_campo(ajustes::actual().custom, campo);
    g_picker.original = c;
    Hsl hh = hsl_de_hex(c);
    g_picker.h = hh.h;
    g_picker.s = hh.s;
    g_picker.l = hh.l;
    float px = swatch_x;
    float py = swatch_y + swatch_h + 6.0f;
    if (py + POPUP_H > g_gfx.alto - 10.0f) py = swatch_y - POPUP_H - 6.0f;
    py = std::clamp(py, 10.0f, std::max(10.0f, g_gfx.alto - POPUP_H - 10.0f));
    px = std::clamp(px, 10.0f, std::max(10.0f, g_gfx.ancho - POPUP_W - 10.0f));
    g_picker.popup_x = px;
    g_picker.popup_y = py;
}

void cancelar_picker() {
    if (!g_picker.abierto) return;
    if (g_picker.campo >= 0 && leer_campo(ajustes::actual().custom, g_picker.campo) != g_picker.original) {
        unsigned c = g_picker.original;
        int campo = g_picker.campo;
        ajustes::cambiar([campo, c](Ajustes& a) { escribir_campo(a.custom, campo, c); });
    }
    // Si se habia pasado a Custom solo por este picker, se vuelve al tema de antes.
    if (g_tema_antes != "custom") {
        std::string t = g_tema_antes;
        ajustes::cambiar([t](Ajustes& a) { a.tema = t; });
    }
    g_picker.abierto = false;
    g_picker.arrastre = 0;
}

void aceptar_picker() {
    // El color ya quedo aplicado en vivo; solo cerrar.
    g_picker.abierto = false;
    g_picker.arrastre = 0;
}

// ---------------------------------------------------------------------
// Audio: listas con "System default" (id vacio) primero, ciclado con < >.

std::vector<grabador::Dispositivo> construir_lista_audio(bool entrada) {
    std::vector<grabador::Dispositivo> r;
    r.push_back({L"", L"System default"});
    for (auto& d : grabador::dispositivos(entrada)) r.push_back(d);
    return r;
}

// El desplegable de dispositivos, dibujado por nosotros con los colores del
// tema (el menu nativo no se puede pintar). Un click en la caja lo abre; otro
// click en la caja, o afuera, lo cierra.
struct Desplegable {
    bool abierto = false;
    bool entrada = false;
    std::vector<grabador::Dispositivo>* lista = nullptr;
    float x = 0, y = 0, w = 0;       // la caja que lo abrio
    float lx = 0, ly = 0, lw = 0, lh = 0;  // la lista desplegada
};
Desplegable g_desplegable;
const float ITEM_DESP = 32.0f;

void desplegar_audio(bool entrada, std::vector<grabador::Dispositivo>* lista, float x, float y) {
    if (g_desplegable.abierto && g_desplegable.entrada == entrada) {
        g_desplegable.abierto = false;
        return;
    }
    g_desplegable.abierto = true;
    g_desplegable.entrada = entrada;
    g_desplegable.lista = lista;
    g_desplegable.x = x;
    g_desplegable.y = y;
}

void elegir_audio(bool entrada, const std::wstring& id) {
    ajustes::cambiar([entrada, id](Ajustes& a) {
        if (entrada) a.entrada = id;
        else a.salida = id;
    });
}

void dibujar_desplegable(Gfx& g) {
    Desplegable& d = g_desplegable;
    int n = 1;
    for (auto& x : *d.lista)
        if (!x.id.empty()) n++;
    d.lw = 300.0f;
    d.lh = n * ITEM_DESP + 8;
    d.lx = d.x;
    d.ly = d.y;
    if (d.ly + d.lh > g.alto - 8) d.ly = std::max(8.0f, g.alto - 8 - d.lh);
    // Sombra y caja.
    g.rect_redondo(d.lx + 2, d.ly + 3, d.lw, d.lh, 8, Color(0x000000, 0.35f));
    g.rect_redondo(d.lx, d.ly, d.lw, d.lh, 8, Color(BG_PANEL()));
    g.borde_redondo(d.lx, d.ly, d.lw, d.lh, 8, Color(BORDE()), 1.0f);
    std::wstring actual_id = d.entrada ? ajustes::actual().entrada : ajustes::actual().salida;
    float y = d.ly + 4;
    auto item = [&](const std::wstring& id, const std::wstring& nombre) {
        bool elegido = id == actual_id;
        if (adentro(g_mouse_x, g_mouse_y, d.lx, y, d.lw, ITEM_DESP)) g.rect(d.lx + 4, y, d.lw - 8, ITEM_DESP, Color(BG_CAMPO()));
        if (elegido) g.renglon(L"\u2713", d.lx + 12, y + 7, 13, Color(ACCENT()));
        g.renglon(nombre, d.lx + 34, y + (ITEM_DESP - 17) / 2.0f, 13.5f, Color(elegido ? ACCENT() : TXT()),
                  DWRITE_FONT_WEIGHT_NORMAL, d.lw - 46);
        bool ent = d.entrada;
        agregar_clic(g_clics_popup, d.lx, y, d.lw, ITEM_DESP, [ent, id]() {
            elegir_audio(ent, id);
            g_desplegable.abierto = false;
        });
        y += ITEM_DESP;
    };
    item(L"", L"System default");
    for (auto& x : *d.lista)
        if (!x.id.empty()) item(x.id, x.nombre);
}

void ciclar_audio(bool entrada, std::vector<grabador::Dispositivo>* lista, int dir) {
    if (!lista || lista->empty()) return;
    std::wstring actual = entrada ? ajustes::actual().entrada : ajustes::actual().salida;
    size_t idx = 0;
    for (size_t i = 0; i < lista->size(); i++)
        if ((*lista)[i].id == actual) { idx = i; break; }
    int n = (int)lista->size();
    idx = (size_t)(((int)idx + dir + n) % n);
    std::wstring nuevo = (*lista)[idx].id;
    if (entrada) ajustes::cambiar([nuevo](Ajustes& a) { a.entrada = nuevo; });
    else ajustes::cambiar([nuevo](Ajustes& a) { a.salida = nuevo; });
}

// ---------------------------------------------------------------------
// Secciones. Cada una dibuja desde (x, y) con el ancho de contenido dado
// y devuelve el y donde termino.

float seccion_tema(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"THEME");
    y += 8;
    auto& temas = ajustes::temas();
    int n = (int)temas.size();
    float paso = ancho_contenido / n;
    float cy = y + 16;
    for (int i = 0; i < n; i++) {
        std::string id = temas[i].first;
        const std::wstring& nombre = temas[i].second;
        float cx = x + paso * i + paso / 2.0f;
        Paleta pal = (id == "custom") ? ajustes::actual().custom : ajustes::paleta_de(id);
        g.recortar(cx - 16, cy - 16, 16, 32);
        g.circulo(cx, cy, 16, Color(pal.bg_app));
        g.destapar();
        g.recortar(cx, cy - 16, 16, 32);
        g.circulo(cx, cy, 16, Color(pal.burbuja_mia));
        g.destapar();
        g.circulo(cx, cy, 4, Color(pal.acento));
        bool activo = ajustes::actual().tema == id;
        if (activo) anillo(g, cx, cy, 19, Color(ACCENT()), 2.0f);
        centrado(g, nombre, cx, cy + 22, 11, Color(activo ? TXT() : TXT_DIM()));
        agregar_clic(g_clics, cx - paso / 2.0f, cy - 22, paso, 44, [id]() {
            ajustes::cambiar([id](Ajustes& a) { a.tema = id; });
        });
    }
    return cy + 22 + 14 + 10;
}

float seccion_colores(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"COLORS");
    y += 6;
    // Se muestran los colores del tema que este activo; tocar uno lo copia
    // al tema Custom y cambia a Custom (como hacia la version vieja).
    const float FILA_C = 28.0f;  // mas apretadas que el resto
    for (int i = 0; i < CC_CANTIDAD; i++) {
        g.renglon(NOMBRE_CAMPO[i], x, y + (FILA_C - 19) / 2.0f, 14, Color(TXT()));
        unsigned c = leer_campo(ajustes::paleta(), i);
        float sw = 22.0f, sw_x = x + ancho_contenido - sw, sw_y = y + (FILA_C - sw) / 2.0f;
        std::wstring hx = wstr_de(ajustes::hex_de(c));
        float hw = g.medir_mono(hx, 13.0f);
        g.renglon_mono(hx, sw_x - 12 - hw, y + (FILA_C - 17) / 2.0f, 13.0f, Color(TXT_DIM()));
        g.rect_redondo(sw_x, sw_y, sw, sw, 5, Color(c));
        g.borde_redondo(sw_x, sw_y, sw, sw, 5, Color(BORDE()), 1.0f);
        int campo = i;
        float csx = sw_x, csy = sw_y, csw = sw, csh = sw;
        agregar_clic(g_clics, sw_x, sw_y, sw, sw, [campo, csx, csy, csw, csh]() {
            if (!g_picker.abierto) abrir_picker(campo, csx, csy, csw, csh);
        });
        y += FILA_C;
    }
    return y;
}

float seccion_fondo(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"CHAT BACKGROUND");
    y += 6;
    const Ajustes& a = ajustes::actual();

    auto fila_radio = [&](const std::wstring& etiqueta, bool elegido, std::function<void()> click) {
        hover_si(g, x, y, ancho_contenido, FILA_LISTA);
        float cy = y + FILA_LISTA / 2.0f;
        anillo(g, x + 9, cy, 8, Color(elegido ? ACCENT() : BORDE()), 1.5f);
        if (elegido) g.circulo(x + 9, cy, 4.5f, Color(ACCENT()));
        g.renglon(etiqueta, x + 28, y + (FILA_LISTA - 14) / 2.0f, 14, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL,
                  ancho_contenido - 28);
        agregar_clic(g_clics, x, y, ancho_contenido, FILA_LISTA, std::move(click));
        y += FILA_LISTA;
    };

    fila_radio(L"None", a.fondo == L"", []() { ajustes::cambiar([](Ajustes& a) { a.fondo = L""; }); });
    fila_radio(L"WhatsApp doodles", a.fondo == L"whatsapp",
               []() { ajustes::cambiar([](Ajustes& a) { a.fondo = L"whatsapp"; }); });
    for (auto& archivo : ajustes::fondos_disponibles()) {
        std::wstring nombre = archivo;
        bool elegido = a.fondo == archivo;
        fila_radio(archivo, elegido, [nombre]() { ajustes::cambiar([nombre](Ajustes& a) { a.fondo = nombre; }); });
    }

    y += 8;
    std::wstring pista = L"Put images in " + ajustes::carpeta_fondos();
    g.renglon(pista, x, y, 12, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_NORMAL, ancho_contenido);
    y += 22;
    float bw = 112, bh = 28;
    hover_si(g, x, y, bw, bh);
    g.rect_redondo(x, y, bw, bh, 6, Color(BG_CAMPO()));
    centrado(g, L"Open folder", x + bw / 2.0f, y + 7, 12.5f, Color(TXT()));
    agregar_clic(g_clics, x, y, bw, bh, []() {
        std::wstring carpeta = ajustes::carpeta_fondos();
        CreateDirectoryW(carpeta.c_str(), nullptr);
        ShellExecuteW(nullptr, L"open", carpeta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    return y + bh;
}

float seccion_letras(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"FONT SIZE");
    y += 6;
    auto fila = [&](const wchar_t* etiqueta, float valor, std::function<void(float)> aplicar) {
        hover_si(g, x, y, ancho_contenido, FILA);
        g.renglon(etiqueta, x, y + (FILA - 16) / 2.0f, 14, Color(TXT()));
        float R = x + ancho_contenido;
        float b = 28.0f, by = y + (FILA - b) / 2.0f;
        float bx1 = R - b, bx0 = R - b - 56 - b;
        g.rect_redondo(bx0, by, b, b, 6, Color(BG_CAMPO()));
        centrado(g, L"−", bx0 + b / 2.0f, by + 5, 15, Color(TXT()));
        g.rect_redondo(bx1, by, b, b, 6, Color(BG_CAMPO()));
        centrado(g, L"+", bx1 + b / 2.0f, by + 5, 15, Color(TXT()));
        wchar_t buf[8];
        swprintf(buf, 8, L"%d", (int)valor);
        centrado(g, buf, (bx0 + b + bx1) / 2.0f, y + (FILA - 14) / 2.0f, 14, Color(TXT()));
        agregar_clic(g_clics, bx0, by, b, b,
                     [aplicar, valor]() { aplicar(std::clamp(valor - 1.0f, 10.0f, 24.0f)); });
        agregar_clic(g_clics, bx1, by, b, b,
                     [aplicar, valor]() { aplicar(std::clamp(valor + 1.0f, 10.0f, 24.0f)); });
        y += FILA;
    };
    fila(L"Contact list", ajustes::actual().letra_lista,
         [](float v) { ajustes::cambiar([v](Ajustes& a) { a.letra_lista = v; }); });
    fila(L"Chat", ajustes::actual().letra_chat,
         [](float v) { ajustes::cambiar([v](Ajustes& a) { a.letra_chat = v; }); });
    return y;
}

float seccion_notificaciones(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"NOTIFICATIONS");
    y += 6;
    hover_si(g, x, y, ancho_contenido, FILA);
    g.renglon(L"Show notifications", x, y + (FILA - 16) / 2.0f, 14, Color(TXT()));
    bool on = ajustes::actual().notificaciones;
    float pw = 42, ph = 22, px = x + ancho_contenido - pw, py = y + (FILA - ph) / 2.0f;
    g.rect_redondo(px, py, pw, ph, ph / 2.0f, Color(on ? ACCENT() : BG_CAMPO()));
    float cx = on ? px + pw - ph / 2.0f : px + ph / 2.0f;
    g.circulo(cx, py + ph / 2.0f, ph / 2.0f - 3, Color(0xffffff));
    agregar_clic(g_clics, x, y, ancho_contenido, FILA,
                 []() { ajustes::cambiar([](Ajustes& a) { a.notificaciones = !a.notificaciones; }); });
    return y + FILA;
}

float seccion_audio(Gfx& g, float x, float y, float ancho_contenido) {
    y = titulo_seccion(g, x, y, L"AUDIO");
    y += 6;
    auto fila = [&](const wchar_t* etiqueta, std::vector<grabador::Dispositivo>* lista, bool entrada) {
        hover_si(g, x, y, ancho_contenido, FILA);
        g.renglon(etiqueta, x, y + (FILA - 16) / 2.0f, 14, Color(TXT()));
        std::wstring actual_id = entrada ? ajustes::actual().entrada : ajustes::actual().salida;
        std::wstring nombre = L"System default";
        for (auto& d : *lista)
            if (d.id == actual_id) { nombre = d.nombre; break; }
        // Un desplegable: caja con el nombre y una flechita; el click abre
        // un menu nativo (oscuro) con todos los dispositivos.
        float R = x + ancho_contenido;
        float cw = 260.0f, ch = 30.0f, cx = R - cw, cy = y + (FILA - ch) / 2.0f;
        g.rect_redondo(cx, cy, cw, ch, 6, Color(BG_CAMPO()));
        g.borde_redondo(cx, cy, cw, ch, 6, Color(BORDE()), 1.0f);
        g.renglon(nombre, cx + 10, cy + (ch - 13.5f) / 2.0f, 13.5f, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL, cw - 40);
        bool abierto = g_desplegable.abierto && g_desplegable.entrada == entrada;
        centrado(g, abierto ? L"▴" : L"▾", cx + cw - 14, cy + 6, 13, Color(TXT_DIM()));
        agregar_clic(g_clics, cx, cy, cw, ch, [entrada, lista, cx, cy, ch]() { desplegar_audio(entrada, lista, cx, cy + ch); });
        y += FILA;
    };
    fila(L"Microphone", &g_mics, true);
    fila(L"Speakers", &g_altavoces, false);
    return y;
}

// ---------------------------------------------------------------------
// El selector de color flotante (popup), dibujado encima de todo.

void dibujar_selector_color(Gfx& g) {
    g.rect(0, 0, g.ancho, g.alto, Color(0x000000, 0.35f));

    float px = g_picker.popup_x, py = g_picker.popup_y;
    g.rect_redondo(px, py, POPUP_W, POPUP_H, 10, Color(BG_PANEL()));
    g.borde_redondo(px, py, POPUP_W, POPUP_H, 10, Color(BORDE()), 1.0f);
    float ix = px + 14, iy = py + 14;

    g.renglon(NOMBRE_CAMPO[g_picker.campo], ix, iy, 13, Color(TXT()), DWRITE_FONT_WEIGHT_MEDIUM);
    float xx = px + POPUP_W - 14 - 20, xy = py + 10;
    centrado(g, L"✕", xx + 10, xy + 3, 12, Color(TXT_DIM()));
    agregar_clic(g_clics_popup, xx, xy, 20, 20, []() { cancelar_picker(); });
    iy += 22 + 8;

    // Cuadrado saturacion/valor: grilla de celdas de 6x6 (mas simple que
    // armar un ID2D1LinearGradientBrush, y el resultado se ve igual).
    g_rect_cuadrado = {ix, iy, SV_LADO, SV_LADO};
    const float CELDA = 6.0f;
    for (float cy = 0; cy < SV_LADO; cy += CELDA) {
        float v = 1.0f - (cy + CELDA / 2.0f) / SV_LADO;
        for (float cx = 0; cx < SV_LADO; cx += CELDA) {
            float s = (cx + CELDA / 2.0f) / SV_LADO;
            g.rect(ix + cx, iy + cy, CELDA + 0.5f, CELDA + 0.5f, Color(rgb_de_hsv(g_picker.h, s, v)));
        }
    }
    g.borde_redondo(ix, iy, SV_LADO, SV_LADO, 4, Color(BORDE()), 1.0f);
    float s_act, v_act;
    hsv_de_hex(color_actual_picker(), s_act, v_act);
    float mx = ix + s_act * SV_LADO, my = iy + (1.0f - v_act) * SV_LADO;
    anillo(g, mx, my, 6, Color(0xffffff), 2.0f);

    // Barra de matiz (vertical, arcoiris).
    float hx = ix + SV_LADO + 8, hy = iy;
    g_rect_hue = {hx, hy, BARRA_ANCHO, SV_LADO};
    for (float cy = 0; cy < SV_LADO; cy += 3.0f) {
        float t = cy / SV_LADO;
        g.rect(hx, hy + cy, BARRA_ANCHO, 3.5f, Color(rgb_de_hsv(t * 360.0f, 1.0f, 1.0f)));
    }
    g.borde_redondo(hx, hy, BARRA_ANCHO, SV_LADO, 4, Color(BORDE()), 1.0f);
    float marca_h = hy + (g_picker.h / 360.0f) * SV_LADO;
    g.rect(hx - 2, marca_h - 2, BARRA_ANCHO + 4, 4, Color(0xffffff));

    // Barra de luminosidad (vertical): blanco arriba, el color en el medio,
    // negro abajo.
    float lx = hx + BARRA_ANCHO + 8, ly = iy;
    g_rect_luz = {lx, ly, BARRA_ANCHO, SV_LADO};
    for (float cy = 0; cy < SV_LADO; cy += 3.0f) {
        float l = 1.0f - cy / SV_LADO;
        g.rect(lx, ly + cy, BARRA_ANCHO, 3.5f, Color(rgb_de_hsl(g_picker.h, g_picker.s, l)));
    }
    g.borde_redondo(lx, ly, BARRA_ANCHO, SV_LADO, 4, Color(BORDE()), 1.0f);
    float marca_l = ly + (1.0f - g_picker.l) * SV_LADO;
    g.rect(lx - 2, marca_l - 2, BARRA_ANCHO + 4, 4, Color(0xffffff));

    iy += SV_LADO + 10;
    unsigned actual = color_actual_picker();
    g.rect_redondo(ix, iy, 26, 26, 5, Color(actual));
    g.borde_redondo(ix, iy, 26, 26, 5, Color(BORDE()), 1.0f);
    std::wstring hx_txt = wstr_de(ajustes::hex_de(actual));
    g.renglon_mono(hx_txt, ix + 26 + 10, iy + 6, 13.5f, Color(TXT()));
    iy += 26 + 12;

    float bw = 70, bh = 28;
    float bx_cancel = px + POPUP_W - 14 - bw - 8 - bw;
    float bx_accept = px + POPUP_W - 14 - bw;
    g.rect_redondo(bx_cancel, iy, bw, bh, 6, Color(BG_CAMPO()));
    centrado(g, L"Cancel", bx_cancel + bw / 2.0f, iy + 7, 12.5f, Color(TXT()));
    agregar_clic(g_clics_popup, bx_cancel, iy, bw, bh, []() { cancelar_picker(); });
    g.rect_redondo(bx_accept, iy, bw, bh, 6, Color(ACCENT()));
    centrado(g, L"Accept", bx_accept + bw / 2.0f, iy + 7, 12.5f, Color(0x0b141a));
    agregar_clic(g_clics_popup, bx_accept, iy, bw, bh, []() { aceptar_picker(); });
}

// ---------------------------------------------------------------------
// El frame completo.

void dibujar_todo() {
    if (!g_gfx.ctx) return;
    float max_scroll_prev = std::max(0.0f, g_contenido_alto - g_gfx.alto);
    g_scroll = std::clamp(g_scroll, 0.0f, max_scroll_prev);

    g_gfx.empezar_frame();
    g_gfx.ctx->Clear(Color(BG_APP()).d2d());

    g_clics.clear();
    float x = PADDING;
    float ancho_contenido = std::max(100.0f, g_gfx.ancho - 2 * PADDING);
    float y = PADDING - g_scroll;
    y = seccion_tema(g_gfx, x, y, ancho_contenido) + ESPACIO_SECCION;
    y = seccion_colores(g_gfx, x, y, ancho_contenido) + ESPACIO_SECCION;
    y = seccion_fondo(g_gfx, x, y, ancho_contenido) + ESPACIO_SECCION;
    y = seccion_letras(g_gfx, x, y, ancho_contenido) + ESPACIO_SECCION;
    y = seccion_notificaciones(g_gfx, x, y, ancho_contenido) + ESPACIO_SECCION;
    y = seccion_audio(g_gfx, x, y, ancho_contenido) + PADDING;
    g_contenido_alto = y + g_scroll;

    if (g_picker.abierto) {
        g_clics_popup.clear();
        dibujar_selector_color(g_gfx);
    } else if (g_desplegable.abierto) {
        g_clics_popup.clear();
        dibujar_desplegable(g_gfx);
    }

    g_gfx.terminar_frame();
}

// ---------------------------------------------------------------------
// Ventana: mensajes.

float escala(HWND h) { return GetDpiForWindow(h) / 96.0f; }

LRESULT CALLBACK procedimiento(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            BOOL oscuro = TRUE;
            DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &oscuro, sizeof oscuro);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(h, nullptr);
            dibujar_todo();
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && g_gfx.ctx) {
                g_gfx.redimensionar();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            float e = escala(h);
            g_mouse_x = GET_X_LPARAM(lp) / e;
            g_mouse_y = GET_Y_LPARAM(lp) / e;
            if (g_picker.abierto && g_picker.arrastre != 0) {
                if (g_picker.arrastre == 1) en_cuadrado(g_mouse_x, g_mouse_y);
                else if (g_picker.arrastre == 2) en_hue(g_mouse_y);
                else if (g_picker.arrastre == 3) en_luz(g_mouse_y);
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            float e = escala(h);
            float mx = GET_X_LPARAM(lp) / e, my = GET_Y_LPARAM(lp) / e;
            if (g_picker.abierto) {
                if (adentro(mx, my, g_rect_cuadrado)) {
                    SetCapture(h);
                    g_picker.arrastre = 1;
                    en_cuadrado(mx, my);
                } else if (adentro(mx, my, g_rect_hue)) {
                    SetCapture(h);
                    g_picker.arrastre = 2;
                    en_hue(my);
                } else if (adentro(mx, my, g_rect_luz)) {
                    SetCapture(h);
                    g_picker.arrastre = 3;
                    en_luz(my);
                } else {
                    for (auto& r : g_clics_popup)
                        if (adentro(mx, my, r.x, r.y, r.w, r.h)) { r.accion(); break; }
                }
            } else if (g_desplegable.abierto) {
                bool tomado = false;
                for (auto& r : g_clics_popup)
                    if (adentro(mx, my, r.x, r.y, r.w, r.h)) { r.accion(); tomado = true; break; }
                // Afuera de la lista: se cierra (y el click no hace otra cosa).
                if (!tomado) g_desplegable.abierto = false;
            } else {
                for (auto& r : g_clics)
                    if (adentro(mx, my, r.x, r.y, r.w, r.h)) { r.accion(); break; }
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_picker.arrastre != 0) {
                g_picker.arrastre = 0;
                ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            if (g_picker.abierto || g_desplegable.abierto) return 0;
            float delta = (float)GET_WHEEL_DELTA_WPARAM(wp);
            float max_scroll = std::max(0.0f, g_contenido_alto - g_gfx.alto);
            g_scroll = std::clamp(g_scroll - delta / 120.0f * 54.0f, 0.0f, max_scroll);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                if (g_picker.abierto) cancelar_picker();
                else if (g_desplegable.abierto) g_desplegable.abierto = false;
                else PostMessageW(h, WM_CLOSE, 0, 0);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            g_gfx = Gfx();
            g_hwnd = nullptr;
            g_picker = EstadoPicker();
            g_clics.clear();
            g_clics_popup.clear();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace ventana_ajustes {

void abrir(HWND principal) {
    if (g_hwnd && IsWindow(g_hwnd)) {
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
        return;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registrada = false;
    if (!registrada) {
        WNDCLASSEXW wc = {sizeof wc};
        wc.lpfnWndProc = procedimiento;
        wc.hInstance = inst;
        wc.lpszClassName = L"kciwapp2-ajustes";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)GetClassLongPtrW(principal, GCLP_HICON);
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        registrada = true;
    }

    int w = 520, h = 720;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT rp;
    if (GetWindowRect(principal, &rp)) {
        x = rp.left + ((rp.right - rp.left) - w) / 2;
        y = rp.top + ((rp.bottom - rp.top) - h) / 2;
    }

    g_hwnd = CreateWindowExW(0, L"kciwapp2-ajustes", L"Settings", WS_OVERLAPPEDWINDOW, x, y, w, h, principal, nullptr,
                             inst, nullptr);
    if (!g_hwnd) return;
    if (!g_gfx.iniciar(g_hwnd)) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
        return;
    }
    g_mics = construir_lista_audio(true);
    g_altavoces = construir_lista_audio(false);
    g_scroll = 0;
    g_contenido_alto = 0;
    g_picker = EstadoPicker();

    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);
}

bool abierta() { return g_hwnd != nullptr && IsWindow(g_hwnd); }

void cerrar() {
    if (g_hwnd && IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
}

}  // namespace ventana_ajustes
