// El campo de texto para escribir, dibujado con DirectWrite: cursor,
// seleccion, portapapeles, varias lineas. Alt+164 y demas llegan por WM_CHAR
// como en cualquier ventana de Windows.
#pragma once
#include <functional>
#include <string>

#include "gfx.h"

struct Campo {
    std::wstring texto;
    size_t cursor = 0;
    size_t ancla = 0;  // el otro extremo de la seleccion (== cursor si no hay)
    bool foco = false;
    std::wstring indicio;  // lo que se muestra gris cuando esta vacio
    float tamano = 15.0f;
    float ancho = 0;
    float alto_max = 200.0f;
    // Se llama con Enter (sin Shift).
    std::function<void()> al_enviar;
    std::function<void()> al_escapar;
    std::function<void()> al_cambiar;

    void poner(const std::wstring& t);
    void insertar(const std::wstring& t);
    void borrar_seleccion();
    bool hay_seleccion() const { return cursor != ancla; }
    std::wstring seleccionado() const;
    void seleccionar_todo();
    void copiar();
    void pegar();

    // Alto que necesita (crece con las lineas hasta alto_max).
    float alto(Gfx& g);
    void dibujar(Gfx& g, float x, float y, float w, float h, unsigned long long ahora_ms);

    // Eventos; devuelven si lo consumieron.
    bool tecla(Gfx& g, WPARAM vk, bool shift, bool ctrl);
    bool caracter(wchar_t c);
    bool click(Gfx& g, float x, float y, bool shift);
    void arrastrar(Gfx& g, float x, float y);
    bool arrastrando = false;

    // Posicion visual del cursor (para el IME).
    void posicion_cursor(Gfx& g, float& x, float& y, float& alto_linea);

   private:
    ComPtr<IDWriteTextLayout> layout;
    std::wstring layout_de;
    float layout_ancho = 0;
    float ox = 0, oy = 0;  // donde se dibujo por ultima vez
    float alto_dibujado = 0;
    unsigned long long dibujado_en = 0;  // `ahora` del frame en que se dibujo
    bool tiene(float x, float y) const { return x >= ox && x < ox + ancho && y >= oy && y < oy + alto_dibujado; }
    float desplazamiento = 0;  // scroll vertical interno cuando pasa de alto_max
    unsigned long long ultimo_movimiento = 0;

    IDWriteTextLayout* armar(Gfx& g);
    size_t indice_en(Gfx& g, float x, float y);
    void mover(size_t a, bool shift);
    size_t linea_arriba_abajo(Gfx& g, int direccion);
};
