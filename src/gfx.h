// Motor grafico: D3D11 + swap chain flip-model con waitable object, Direct2D
// para dibujar, DirectWrite para texto y WIC para decodificar imagenes.
// Todo se dibuja en DIPs; la escala DPI la aplica el contexto.
#pragma once
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct Color {
    float r, g, b, a;
    Color(unsigned hex, float alfa = 1.0f)
        : r(((hex >> 16) & 255) / 255.0f), g(((hex >> 8) & 255) / 255.0f), b((hex & 255) / 255.0f), a(alfa) {}
    Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    D2D1_COLOR_F d2d() const { return D2D1::ColorF(r, g, b, a); }
    unsigned clave() const {
        return ((unsigned)(r * 255) << 24) | ((unsigned)(g * 255) << 16) | ((unsigned)(b * 255) << 8) |
               (unsigned)(a * 255);
    }
};

// Una imagen decodificada en un hilo de fondo, lista para subir a la GPU.
struct Pixeles {
    int ancho = 0, alto = 0;
    std::vector<unsigned char> bgra;
    // Para animaciones (WebP/GIF): cada cuadro y su duracion en ms.
    std::vector<std::vector<unsigned char>> cuadros;
    std::vector<int> duraciones;
    bool vacio() const { return ancho == 0 || (bgra.empty() && cuadros.empty()); }
};

struct Gfx {
    HWND hwnd = nullptr;
    float dpi = 96.0f;
    float ancho = 0, alto = 0;  // en DIPs

    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGISwapChain2> swap;
    HANDLE espera_frame = nullptr;
    ComPtr<ID2D1Factory1> fabrica;
    ComPtr<ID2D1Device> dispositivo;
    ComPtr<ID2D1DeviceContext> ctx;
    ComPtr<ID2D1Bitmap1> destino;
    ComPtr<IDWriteFactory3> dwrite;
    ComPtr<IWICImagingFactory> wic;
    // Los layouts de texto se pueden armar desde otros hilos (DirectWrite lo
    // permite); lo que NO se puede tocar desde otro hilo es el contexto D2D
    // ni los pinceles. Este pincel se crea al inicio para los links.
    ComPtr<ID2D1SolidColorBrush> pincel_enlace;

    bool iniciar(HWND h);
    void redimensionar();
    void empezar_frame();
    void terminar_frame();

    // Pinceles y formatos cacheados por color / tamano.
    ID2D1SolidColorBrush* pincel(Color c);
    IDWriteTextFormat* formato(float tamano, DWRITE_FONT_WEIGHT peso = DWRITE_FONT_WEIGHT_NORMAL,
                               const wchar_t* fuente = L"Segoe UI");
    ComPtr<IDWriteTextLayout> texto(const std::wstring& s, float tamano, float ancho_max,
                                    DWRITE_FONT_WEIGHT peso = DWRITE_FONT_WEIGHT_NORMAL,
                                    float alto_max = 100000.0f);
    void dibujar_texto(IDWriteTextLayout* l, float x, float y, Color c);
    // Un renglon suelto: mide y dibuja. Devuelve el ancho.
    float renglon(const std::wstring& s, float x, float y, float tamano, Color c,
                  DWRITE_FONT_WEIGHT peso = DWRITE_FONT_WEIGHT_NORMAL, float ancho_max = 100000.0f);
    float medir(const std::wstring& s, float tamano, DWRITE_FONT_WEIGHT peso = DWRITE_FONT_WEIGHT_NORMAL);
    // Lo mismo pero en fuente monoespaciada (codigos, hex).
    float renglon_mono(const std::wstring& s, float x, float y, float tamano, Color c);
    float renglon_fuente(const wchar_t* fuente, const std::wstring& s, float x, float y, float tamano, Color c);
    float medir_fuente(const wchar_t* fuente, const std::wstring& s, float tamano);
    float medir_mono(const std::wstring& s, float tamano);

    void rect(float x, float y, float w, float h, Color c);
    void rect_redondo(float x, float y, float w, float h, float radio, Color c);
    void borde_redondo(float x, float y, float w, float h, float radio, Color c, float grosor = 1.0f);
    void circulo(float cx, float cy, float radio, Color c);
    // Triangulo relleno (para el play): tres vertices.
    void triangulo(float x1, float y1, float x2, float y2, float x3, float y3, Color c);
    void linea(float x1, float y1, float x2, float y2, Color c, float grosor = 1.0f);
    // Lupa de trazo: aro de radio r centrado en (cx, cy) y el mango hacia abajo a la derecha.
    void lupa(float cx, float cy, float r, Color c, float grosor = 1.6f);
    void recortar(float x, float y, float w, float h);
    void destapar();
    void recortar_redondo(float x, float y, float w, float h, float radio);
    void destapar_redondo();
    void bitmap(ID2D1Bitmap* b, float x, float y, float w, float h, float opacidad = 1.0f);
    void bitmap_circular(ID2D1Bitmap* b, float cx, float cy, float radio);

    // Decodificar (en cualquier hilo) y subir (solo en el hilo de la ventana).
    Pixeles decodificar(const std::string& datos, bool animado = false);
    ComPtr<ID2D1Bitmap1> subir(const Pixeles& p, size_t cuadro = 0);

    D2D1_MATRIX_3X2_F transformacion;

   private:
    std::map<unsigned, ComPtr<ID2D1SolidColorBrush>> pinceles;
    std::map<std::wstring, ComPtr<IDWriteTextFormat>> formatos;
    std::mutex formatos_mu;
    std::vector<ComPtr<ID2D1Layer>> capas;
    int capas_usadas = 0;
};
