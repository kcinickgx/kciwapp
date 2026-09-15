#include "campo.h"

#include <algorithm>

namespace {
const float RELLENO_X = 12.0f;
const float RELLENO_Y = 9.0f;

// Los pares sustitutos y los emojis compuestos no se parten: se mueve el
// cursor de a "cluster". DirectWrite sabe donde cortan.
}  // namespace

IDWriteTextLayout* Campo::armar(Gfx& g) {
    float w = std::max(10.0f, ancho - RELLENO_X * 2);
    if (!layout || layout_de != texto || layout_ancho != w) {
        layout = g.texto(texto.empty() ? L" " : texto, tamano, w);
        layout_de = texto;
        layout_ancho = w;
    }
    return layout.Get();
}

void Campo::poner(const std::wstring& t) {
    texto = t;
    cursor = ancla = texto.size();
    layout.Reset();
    if (al_cambiar) al_cambiar();
}

void Campo::borrar_seleccion() {
    if (!hay_seleccion()) return;
    size_t a = std::min(cursor, ancla), b = std::max(cursor, ancla);
    texto.erase(a, b - a);
    cursor = ancla = a;
}

void Campo::insertar(const std::wstring& t) {
    borrar_seleccion();
    texto.insert(cursor, t);
    cursor += t.size();
    ancla = cursor;
    ultimo_movimiento = GetTickCount64();
    if (al_cambiar) al_cambiar();
}

std::wstring Campo::seleccionado() const {
    if (!hay_seleccion()) return L"";
    size_t a = std::min(cursor, ancla), b = std::max(cursor, ancla);
    return texto.substr(a, b - a);
}

void Campo::seleccionar_todo() {
    ancla = 0;
    cursor = texto.size();
}

void Campo::copiar() {
    std::wstring s = seleccionado();
    if (s.empty()) return;
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

void Campo::pegar() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) return;
    HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            std::wstring s = p;
            // Los saltos de Windows a saltos simples.
            std::wstring limpio;
            for (wchar_t c : s)
                if (c != L'\r') limpio += c;
            insertar(limpio);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

float Campo::alto(Gfx& g) {
    IDWriteTextLayout* l = armar(g);
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    float h = m.height + RELLENO_Y * 2;
    return std::min(std::max(h, tamano + RELLENO_Y * 2 + 6), alto_max);
}

void Campo::dibujar(Gfx& g, float x, float y, float w, float h, unsigned long long ahora_ms) {
    ancho = w;
    ox = x;
    oy = y;
    IDWriteTextLayout* l = armar(g);
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    // Que el cursor quede a la vista cuando el texto pasa del alto.
    float cx, cy, ch;
    posicion_cursor(g, cx, cy, ch);
    float interior = h - RELLENO_Y * 2;
    float cursor_rel = cy - (y + RELLENO_Y);
    if (m.height <= interior) desplazamiento = 0;
    else {
        float abs_cursor = cursor_rel + desplazamiento;
        if (abs_cursor + ch > desplazamiento + interior) desplazamiento = abs_cursor + ch - interior;
        if (abs_cursor < desplazamiento) desplazamiento = abs_cursor;
        desplazamiento = std::min(desplazamiento, m.height - interior);
    }
    float tx = x + RELLENO_X, ty = y + RELLENO_Y - desplazamiento;

    g.recortar(x, y, w, h);
    if (texto.empty()) {
        g.renglon(indicio, tx, y + RELLENO_Y, tamano, Color(0x8696a0));
    } else {
        if (hay_seleccion()) {
            size_t a = std::min(cursor, ancla), b = std::max(cursor, ancla);
            UINT32 cuantos = 0;
            l->HitTestTextRange((UINT32)a, (UINT32)(b - a), tx, ty, nullptr, 0, &cuantos);
            std::vector<DWRITE_HIT_TEST_METRICS> cajas(cuantos);
            l->HitTestTextRange((UINT32)a, (UINT32)(b - a), tx, ty, cajas.data(), cuantos, &cuantos);
            for (auto& c : cajas) g.rect(c.left, c.top, c.width, c.height, Color(0x00a884, 0.35f));
        }
        g.dibujar_texto(l, tx, ty, Color(0xe9edef));
    }
    if (foco && ((ahora_ms - ultimo_movimiento) / 530) % 2 == 0) {
        posicion_cursor(g, cx, cy, ch);
        g.rect(cx, cy - desplazamiento, 1.5f, ch, Color(0xe9edef));
    }
    g.destapar();
}

void Campo::posicion_cursor(Gfx& g, float& x, float& y, float& alto_linea) {
    IDWriteTextLayout* l = armar(g);
    DWRITE_HIT_TEST_METRICS m;
    float px = 0, py = 0;
    l->HitTestTextPosition((UINT32)cursor, FALSE, &px, &py, &m);
    x = ox + RELLENO_X + px;
    y = oy + RELLENO_Y + py;
    alto_linea = m.height;
}

size_t Campo::indice_en(Gfx& g, float x, float y) {
    IDWriteTextLayout* l = armar(g);
    BOOL final = FALSE, dentro = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    l->HitTestPoint(x - ox - RELLENO_X, y - oy - RELLENO_Y + desplazamiento, &final, &dentro, &m);
    size_t i = m.textPosition + (final ? m.length : 0);
    return std::min(i, texto.size());
}

void Campo::mover(size_t a, bool shift) {
    cursor = std::min(a, texto.size());
    if (!shift) ancla = cursor;
    ultimo_movimiento = GetTickCount64();
}

size_t Campo::linea_arriba_abajo(Gfx& g, int direccion) {
    IDWriteTextLayout* l = armar(g);
    DWRITE_HIT_TEST_METRICS m;
    float px = 0, py = 0;
    l->HitTestTextPosition((UINT32)cursor, FALSE, &px, &py, &m);
    float ny = py + m.height / 2 + direccion * m.height;
    if (ny < 0) return 0;
    BOOL final = FALSE, dentro = FALSE;
    DWRITE_HIT_TEST_METRICS d;
    l->HitTestPoint(px, ny, &final, &dentro, &d);
    if (!dentro && direccion > 0) return texto.size();
    return d.textPosition + (final ? d.length : 0);
}

// Cuantas unidades UTF-16 ocupa el cluster que empieza/termina en `i`.
static size_t cluster_atras(const std::wstring& t, size_t i) {
    if (i == 0) return 0;
    size_t j = i - 1;
    if (j > 0 && t[j] >= 0xDC00 && t[j] <= 0xDFFF) j--;
    // Selectores de variacion y uniones (emojis compuestos).
    while (j > 0 && (t[j] == 0xFE0F || t[j] == 0x200D || (t[j] >= 0xDC00 && t[j] <= 0xDFFF && j > 0 && t[j - 1] >= 0xD800 && t[j - 1] <= 0xDBFF && j >= 2 && t[j - 2] == 0x200D))) {
        j--;
        if (j > 0 && t[j] >= 0xDC00 && t[j] <= 0xDFFF) j--;
    }
    return j;
}

static size_t cluster_adelante(const std::wstring& t, size_t i) {
    if (i >= t.size()) return t.size();
    size_t j = i + 1;
    if (t[i] >= 0xD800 && t[i] <= 0xDBFF && j < t.size()) j++;
    while (j < t.size() && (t[j] == 0xFE0F || t[j] == 0x200D)) {
        j++;
        if (t[j - 1] == 0x200D && j < t.size()) {
            j++;
            if (t[j - 1] >= 0xD800 && t[j - 1] <= 0xDBFF && j < t.size()) j++;
        }
    }
    return std::min(j, t.size());
}

bool Campo::tecla(Gfx& g, WPARAM vk, bool shift, bool ctrl) {
    switch (vk) {
        case VK_LEFT:
            if (hay_seleccion() && !shift) mover(std::min(cursor, ancla), false);
            else mover(cluster_atras(texto, cursor), shift);
            return true;
        case VK_RIGHT:
            if (hay_seleccion() && !shift) mover(std::max(cursor, ancla), false);
            else mover(cluster_adelante(texto, cursor), shift);
            return true;
        case VK_UP: mover(linea_arriba_abajo(g, -1), shift); return true;
        case VK_DOWN: mover(linea_arriba_abajo(g, 1), shift); return true;
        case VK_HOME:
            if (ctrl) mover(0, shift);
            else {
                size_t i = cursor;
                while (i > 0 && texto[i - 1] != L'\n') i--;
                mover(i, shift);
            }
            return true;
        case VK_END:
            if (ctrl) mover(texto.size(), shift);
            else {
                size_t i = cursor;
                while (i < texto.size() && texto[i] != L'\n') i++;
                mover(i, shift);
            }
            return true;
        case VK_BACK:
            if (hay_seleccion()) borrar_seleccion();
            else if (cursor > 0) {
                size_t a = ctrl ? 0 : cluster_atras(texto, cursor);
                if (ctrl) {
                    a = cursor;
                    while (a > 0 && texto[a - 1] == L' ') a--;
                    while (a > 0 && texto[a - 1] != L' ' && texto[a - 1] != L'\n') a--;
                }
                texto.erase(a, cursor - a);
                cursor = ancla = a;
            }
            ultimo_movimiento = GetTickCount64();
            if (al_cambiar) al_cambiar();
            return true;
        case VK_DELETE:
            if (hay_seleccion()) borrar_seleccion();
            else if (cursor < texto.size()) texto.erase(cursor, cluster_adelante(texto, cursor) - cursor);
            ultimo_movimiento = GetTickCount64();
            if (al_cambiar) al_cambiar();
            return true;
        case VK_RETURN:
            if (shift) insertar(L"\n");
            else if (al_enviar) al_enviar();
            return true;
        case VK_ESCAPE:
            if (al_escapar) al_escapar();
            return true;
        case 'A':
            if (ctrl) { seleccionar_todo(); return true; }
            return false;
        case 'C':
            if (ctrl) { copiar(); return true; }
            return false;
        case 'X':
            if (ctrl) { copiar(); borrar_seleccion(); if (al_cambiar) al_cambiar(); return true; }
            return false;
        case 'V':
            if (ctrl) { pegar(); return true; }
            return false;
    }
    return false;
}

bool Campo::caracter(wchar_t c) {
    if (c == L'\r' || c == L'\n' || c == L'\t' || c == 27 || c == 8) return false;
    if (c < 32) return false;
    insertar(std::wstring(1, c));
    return true;
}

bool Campo::click(Gfx& g, float x, float y, bool shift) {
    mover(indice_en(g, x, y), shift);
    arrastrando = true;
    return true;
}

void Campo::arrastrar(Gfx& g, float x, float y) {
    if (!arrastrando) return;
    mover(indice_en(g, x, y), true);
}
