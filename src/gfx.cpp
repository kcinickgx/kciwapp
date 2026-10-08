#include "gfx.h"

#include <shlwapi.h>

#include <cmath>

// Lo que dice que la placa ya no esta: TDR, driver reinstalado o reiniciado.
static bool es_perdida(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
           hr == D2DERR_RECREATE_TARGET;
}

static unsigned g_generaciones = 0;

bool Gfx::iniciar(HWND h) {
    hwnd = h;
    dpi = (float)GetDpiForWindow(h);

    // Lo que no depende de la placa: se crea una sola vez.
    D2D1_FACTORY_OPTIONS opciones = {};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opciones, &fabrica);
    if (FAILED(hr)) return false;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), &dwrite);
    if (FAILED(hr)) return false;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;
    return crear_dispositivo(true);
}

// Todo lo que vive en la placa: D3D, el swap chain de la ventana y el
// contexto D2D. Sirve para el arranque y para volver de un reinicio del driver.
bool Gfx::crear_dispositivo(bool permitir_warp) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL niveles[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                   D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> nuevo;
    ComPtr<ID3D11DeviceContext> d3dctx;
    bool por_hardware = true;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, niveles, 4, D3D11_SDK_VERSION,
                                   &nuevo, nullptr, &d3dctx);
    if (FAILED(hr)) {
        // Volviendo de un reset la placa puede tardar: se reintenta, no se
        // cae a WARP para siempre.
        if (!permitir_warp) return false;
        por_hardware = false;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, niveles, 4, D3D11_SDK_VERSION, &nuevo,
                               nullptr, &d3dctx);
        if (FAILED(hr)) return false;
    }
    ComPtr<IDXGIDevice1> dxgi;
    nuevo.As(&dxgi);
    ComPtr<ID2D1Device> nuevo_d2d;
    if (FAILED(fabrica->CreateDevice(dxgi.Get(), &nuevo_d2d))) return false;
    ComPtr<ID2D1DeviceContext> nuevo_ctx;
    if (FAILED(nuevo_d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &nuevo_ctx))) return false;
    ComPtr<ID2D1SolidColorBrush> nuevo_enlace;
    nuevo_ctx->CreateSolidColorBrush(Color(0x53bdeb).d2d(), &nuevo_enlace);

    // Recien ahora se suelta lo viejo: la ventana admite un solo swap chain.
    soltar_dispositivo();
    d3d = nuevo;
    dispositivo = nuevo_d2d;
    ctx = nuevo_ctx;
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    if (pincel_enlace) pinceles_viejos.push_back(pincel_enlace);
    pincel_enlace = nuevo_enlace;
    generacion = ++g_generaciones;

    acelerado = por_hardware;
    ComPtr<IDXGIAdapter> adaptador;
    dxgi->GetAdapter(&adaptador);
    ComPtr<IDXGIFactory2> fabrica_dxgi;
    adaptador->GetParent(IID_PPV_ARGS(&fabrica_dxgi));
    DXGI_ADAPTER_DESC desc = {};
    if (SUCCEEDED(adaptador->GetDesc(&desc))) {
        placa = desc.Description;
        // El "adaptador basico de Microsoft" tambien es software.
        if (desc.VendorId == 0x1414) acelerado = false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> swap1;
    hr = fabrica_dxgi->CreateSwapChainForHwnd(d3d.Get(), hwnd, &sd, nullptr, nullptr, &swap1);
    if (FAILED(hr)) return false;  // el contexto ya es el nuevo: el swap se reintenta en el proximo frame
    swap1.As(&swap);
    swap->SetMaximumFrameLatency(1);
    espera_frame = swap->GetFrameLatencyWaitableObject();
    fabrica_dxgi->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    perdido = false;
    redimensionar();
    return true;
}

void Gfx::soltar_dispositivo() {
    if (ctx) ctx->SetTarget(nullptr);
    destino.Reset();
    pinceles.clear();
    capas.clear();
    capas_usadas = 0;
    if (espera_frame) {
        CloseHandle(espera_frame);
        espera_frame = nullptr;
    }
    swap.Reset();
    ctx.Reset();
    dispositivo.Reset();
    if (d3d) {
        // Que la destruccion diferida del swap chain se haga ya: si no, crear
        // el nuevo para la misma ventana puede fallar.
        ComPtr<ID3D11DeviceContext> inmediato;
        d3d->GetImmediateContext(&inmediato);
        inmediato->ClearState();
        inmediato->Flush();
    }
    d3d.Reset();
}

// La placa se perdio: se arma todo de nuevo. Si el driver todavia no volvio,
// se reintenta en el proximo frame sin quemar CPU.
void Gfx::recuperar() {
    if (crear_dispositivo(false)) return;
    perdido = true;
    Sleep(50);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Gfx::redimensionar() {
    if (!swap) return;
    RECT r;
    GetClientRect(hwnd, &r);
    UINT px_ancho = r.right - r.left, px_alto = r.bottom - r.top;
    if (px_ancho == 0 || px_alto == 0) return;
    dpi = (float)GetDpiForWindow(hwnd);
    ancho = px_ancho * 96.0f / dpi;
    alto = px_alto * 96.0f / dpi;
    ctx->SetTarget(nullptr);
    destino.Reset();
    // Con la placa muerta esto falla y GetBuffer no da superficie: sin estos
    // chequeos, CreateBitmapFromDxgiSurface(nullptr) tiraba el programa.
    HRESULT hr = swap->ResizeBuffers(0, px_ancho, px_alto, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr)) {
        if (es_perdida(hr)) {
            perdido = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }
    ComPtr<IDXGISurface> superficie;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&superficie))) || !superficie) return;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    if (FAILED(ctx->CreateBitmapFromDxgiSurface(superficie.Get(), &bp, &destino))) return;
    ctx->SetTarget(destino.Get());
    ctx->SetDpi(dpi, dpi);
}

void Gfx::empezar_frame() {
    if (perdido) recuperar();
    if (!destino) redimensionar();
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    transformacion = D2D1::Matrix3x2F::Identity();
    capas_usadas = 0;
}

void Gfx::terminar_frame() {
    HRESULT hr = ctx->EndDraw();
    if (es_perdida(hr)) {
        perdido = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (!swap || !destino) return;
    DXGI_PRESENT_PARAMETERS pp = {};
    hr = swap->Present1(vsync ? 1 : 0, 0, &pp);
    if (es_perdida(hr)) {
        perdido = true;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

ID2D1SolidColorBrush* Gfx::pincel(Color c) {
    auto& p = pinceles[c.clave()];
    if (!p) ctx->CreateSolidColorBrush(c.d2d(), &p);
    // Nunca null (D2D se cae con un pincel null): si no se pudo crear, el de los links.
    return p ? p.Get() : pincel_enlace.Get();
}

// La tabla de reemplazo de fuentes: primero la nuestra (los bloques de
// emoji van a Segoe UI Emoji), despues la del sistema para todo lo demas.
IDWriteFontFallback* Gfx::fallback() {
    if (fallback_emoji) return fallback_emoji.Get();
    ComPtr<IDWriteFontFallbackBuilder> armador;
    if (FAILED(dwrite->CreateFontFallbackBuilder(&armador))) return nullptr;
    // Twemoji Mozilla, al lado del exe: solo para las banderas.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring ttf = exe;
    size_t corte = ttf.find_last_of(L'\\');
    ttf = (corte == std::wstring::npos ? L"." : ttf.substr(0, corte)) + L"\\TwemojiMozilla.ttf";
    if (GetFileAttributesW(ttf.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // IDWriteFontSetBuilder1 (dwrite_3) es el que toma un archivo entero.
        ComPtr<IDWriteFontSetBuilder> juego;
        ComPtr<IDWriteFontSetBuilder1> juego1;
        ComPtr<IDWriteFontFile> archivo;
        if (SUCCEEDED(dwrite->CreateFontSetBuilder(&juego)) && SUCCEEDED(juego.As(&juego1)) &&
            SUCCEEDED(dwrite->CreateFontFileReference(ttf.c_str(), nullptr, &archivo)) && SUCCEEDED(juego1->AddFontFile(archivo.Get()))) {
            ComPtr<IDWriteFontSet> listo;
            if (SUCCEEDED(juego1->CreateFontSet(&listo))) dwrite->CreateFontCollectionFromFontSet(listo.Get(), &coleccion_banderas);
        }
    }
    if (coleccion_banderas) {
        DWRITE_UNICODE_RANGE banderas[] = {
            {0x1F1E6, 0x1F1FF},  // los pares de letras que forman cada bandera
            {0xE0020, 0xE007F},  // las etiquetas de las banderas de region
        };
        const wchar_t* twemoji[] = {L"Twemoji Mozilla"};
        armador->AddMapping(banderas, ARRAYSIZE(banderas), twemoji, 1, coleccion_banderas.Get(), nullptr, nullptr, 1.0f);
    }
    DWRITE_UNICODE_RANGE rangos[] = {
        {0x1F000, 0x1FAFF},  // pictogramas, caritas, simbolos extendidos A y B
        {0x1FC00, 0x1FFFF},  // lo que venga despues
        // Los simbolos viejos (2600-27BF) no se tocan: ya andan, y ahi
        // viven tildes y flechas que la interfaz dibuja como texto.
    };
    const wchar_t* familias[] = {L"Segoe UI Emoji"};
    armador->AddMapping(rangos, ARRAYSIZE(rangos), familias, 1, nullptr, nullptr, nullptr, 1.0f);
    ComPtr<IDWriteFontFallback> sistema;
    if (SUCCEEDED(dwrite->GetSystemFontFallback(&sistema))) armador->AddMappings(sistema.Get());
    armador->CreateFontFallback(&fallback_emoji);
    return fallback_emoji.Get();
}

IDWriteTextFormat* Gfx::formato(float tamano, DWRITE_FONT_WEIGHT peso, const wchar_t* fuente) {
    std::wstring clave = fuente + std::to_wstring((int)(tamano * 10)) + L"/" + std::to_wstring((int)peso);
    std::lock_guard<std::mutex> candado(formatos_mu);
    auto& f = formatos[clave];
    if (!f) {
        dwrite->CreateTextFormat(fuente, nullptr, peso, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, tamano,
                                 L"es-AR", &f);
        if (f) {
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
            ComPtr<IDWriteTextFormat1> f1;
            if (SUCCEEDED(f.As(&f1)) && fallback()) f1->SetFontFallback(fallback());
        }
    }
    return f.Get();
}

ComPtr<IDWriteTextLayout> Gfx::texto(const std::wstring& s, float tamano, float ancho_max, DWRITE_FONT_WEIGHT peso,
                                     float alto_max) {
    ComPtr<IDWriteTextLayout> l;
    dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), formato(tamano, peso), ancho_max, alto_max, &l);
    return l;
}

void Gfx::dibujar_texto(IDWriteTextLayout* l, float x, float y, Color c) {
    if (!l) return;
    ctx->DrawTextLayout(D2D1::Point2F(x, y), l, pincel(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float Gfx::renglon(const std::wstring& s, float x, float y, float tamano, Color c, DWRITE_FONT_WEIGHT peso,
                   float ancho_max) {
    auto l = texto(s, tamano, ancho_max, peso);
    if (!l) return 0;
    l->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (ancho_max < 100000.0f) {
        DWRITE_TRIMMING recorte = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> puntos;
        dwrite->CreateEllipsisTrimmingSign(l.Get(), &puntos);
        l->SetTrimming(&recorte, puntos.Get());
    }
    dibujar_texto(l.Get(), x, y, c);
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::renglon_mono(const std::wstring& s, float x, float y, float tamano, Color c) {
    ComPtr<IDWriteTextLayout> l;
    dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), formato(tamano, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas"), 100000.0f, 100000.0f, &l);
    if (!l) return 0;
    dibujar_texto(l.Get(), x, y, c);
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::medir_fuente(const wchar_t* fuente, const std::wstring& s, float tamano) {
    ComPtr<IDWriteTextLayout> l;
    dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), formato(tamano, DWRITE_FONT_WEIGHT_NORMAL, fuente), 100000.0f, 100000.0f, &l);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::renglon_fuente(const wchar_t* fuente, const std::wstring& s, float x, float y, float tamano, Color c) {
    ComPtr<IDWriteTextLayout> l;
    dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), formato(tamano, DWRITE_FONT_WEIGHT_NORMAL, fuente), 100000.0f, 100000.0f, &l);
    if (!l) return 0;
    dibujar_texto(l.Get(), x, y, c);
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::medir_mono(const std::wstring& s, float tamano) {
    ComPtr<IDWriteTextLayout> l;
    dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), formato(tamano, DWRITE_FONT_WEIGHT_NORMAL, L"Consolas"), 100000.0f, 100000.0f, &l);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::medir(const std::wstring& s, float tamano, DWRITE_FONT_WEIGHT peso) {
    auto l = texto(s, tamano, 100000.0f, peso);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m;
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Gfx::rect(float x, float y, float w, float h, Color c) {
    ctx->FillRectangle(D2D1::RectF(x, y, x + w, y + h), pincel(c));
}

void Gfx::rect_redondo(float x, float y, float w, float h, float radio, Color c) {
    ctx->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radio, radio), pincel(c));
}

void Gfx::borde_redondo(float x, float y, float w, float h, float radio, Color c, float grosor) {
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radio, radio), pincel(c), grosor);
}

void Gfx::circulo(float cx, float cy, float radio, Color c) {
    ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radio, radio), pincel(c));
}

void Gfx::triangulo(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    ComPtr<ID2D1PathGeometry> geo;
    fabrica->CreatePathGeometry(&geo);
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(&sink);
    sink->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x2, y2));
    sink->AddLine(D2D1::Point2F(x3, y3));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    ctx->FillGeometry(geo.Get(), pincel(c));
}

void Gfx::linea(float x1, float y1, float x2, float y2, Color c, float grosor) {
    ctx->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), pincel(c), grosor);
}

void Gfx::lupa(float cx, float cy, float r, Color c, float grosor) {
    ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), pincel(c), grosor);
    ctx->DrawLine(D2D1::Point2F(cx + r * 0.72f, cy + r * 0.72f), D2D1::Point2F(cx + r * 1.7f, cy + r * 1.7f), pincel(c), grosor + 0.4f);
}

void Gfx::recortar(float x, float y, float w, float h) {
    ctx->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Gfx::destapar() { ctx->PopAxisAlignedClip(); }

void Gfx::recortar_redondo(float x, float y, float w, float h, float radio) {
    ComPtr<ID2D1RoundedRectangleGeometry> g;
    fabrica->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radio, radio), &g);
    if ((int)capas.size() <= capas_usadas) {
        ComPtr<ID2D1Layer> capa;
        ctx->CreateLayer(nullptr, &capa);
        capas.push_back(capa);
    }
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), capas[capas_usadas].Get());
    capas_usadas++;
}

void Gfx::destapar_redondo() {
    ctx->PopLayer();
    capas_usadas--;
}

void Gfx::bitmap(ID2D1Bitmap* b, float x, float y, float w, float h, float opacidad) {
    if (!b) return;
    ctx->DrawBitmap(b, D2D1::RectF(x, y, x + w, y + h), opacidad, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

void Gfx::bitmap_circular(ID2D1Bitmap* b, float cx, float cy, float radio) {
    if (!b) return;
    ComPtr<ID2D1EllipseGeometry> g;
    fabrica->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), radio, radio), &g);
    if ((int)capas.size() <= capas_usadas) {
        ComPtr<ID2D1Layer> capa;
        ctx->CreateLayer(nullptr, &capa);
        capas.push_back(capa);
    }
    ctx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), g.Get()), capas[capas_usadas].Get());
    capas_usadas++;
    // La foto se recorta al cuadrado central, sin deformar.
    D2D1_SIZE_F t = b->GetSize();
    float lado = std::min(t.width, t.height);
    D2D1_RECT_F fuente = D2D1::RectF((t.width - lado) / 2, (t.height - lado) / 2, (t.width + lado) / 2,
                                     (t.height + lado) / 2);
    ctx->DrawBitmap(b, D2D1::RectF(cx - radio, cy - radio, cx + radio, cy + radio), 1.0f,
                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &fuente);
    ctx->PopLayer();
    capas_usadas--;
}

Pixeles Gfx::decodificar(const std::string& datos, bool animado) {
    Pixeles p;
    if (datos.empty() || !wic) return p;
    ComPtr<IStream> flujo(SHCreateMemStream((const BYTE*)datos.data(), (UINT)datos.size()));
    if (!flujo) return p;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromStream(flujo.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return p;
    UINT cuadros = 0;
    dec->GetFrameCount(&cuadros);
    if (cuadros == 0) return p;
    if (!animado) cuadros = 1;

    // Para animaciones se compone cada cuadro sobre el anterior (los WebP
    // animados suelen traer solo la diferencia).
    std::vector<unsigned char> lienzo;
    for (UINT i = 0; i < cuadros; i++) {
        ComPtr<IWICBitmapFrameDecode> cuadro;
        if (FAILED(dec->GetFrame(i, &cuadro))) break;
        ComPtr<IWICFormatConverter> conv;
        wic->CreateFormatConverter(&conv);
        if (FAILED(conv->Initialize(cuadro.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom)))
            break;
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w == 0 || h == 0) break;
        std::vector<unsigned char> px((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) break;
        if (i == 0) {
            p.ancho = (int)w;
            p.alto = (int)h;
            if (!animado) {
                p.bgra = std::move(px);
                break;
            }
            lienzo = px;
        } else {
            // Posicion del cuadro dentro del lienzo, si la trae.
            int cx = 0, cy = 0;
            ComPtr<IWICMetadataQueryReader> meta;
            if (SUCCEEDED(cuadro->GetMetadataQueryReader(&meta))) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Left", &v))) cx = v.uiVal;
                PropVariantClear(&v);
                if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Top", &v))) cy = v.uiVal;
                PropVariantClear(&v);
            }
            if ((int)w == p.ancho && (int)h == p.alto) {
                // Cuadro completo: se apoya sobre el anterior respetando alfa.
                for (size_t k = 0; k < px.size(); k += 4) {
                    unsigned a = px[k + 3];
                    if (a == 255) {
                        memcpy(&lienzo[k], &px[k], 4);
                    } else if (a > 0) {
                        for (int c = 0; c < 4; c++)
                            lienzo[k + c] = (unsigned char)(px[k + c] + lienzo[k + c] * (255 - a) / 255);
                    }
                }
            } else {
                for (UINT y = 0; y < h && (int)(cy + y) < p.alto; y++)
                    for (UINT x = 0; x < w && (int)(cx + x) < p.ancho; x++) {
                        size_t s = ((size_t)y * w + x) * 4, d = (((size_t)(cy + y)) * p.ancho + cx + x) * 4;
                        unsigned a = px[s + 3];
                        if (a == 255) memcpy(&lienzo[d], &px[s], 4);
                        else if (a > 0)
                            for (int c = 0; c < 4; c++)
                                lienzo[d + c] = (unsigned char)(px[s + c] + lienzo[d + c] * (255 - a) / 255);
                    }
            }
        }
        int dur = 100;
        ComPtr<IWICMetadataQueryReader> meta;
        if (SUCCEEDED(cuadro->GetMetadataQueryReader(&meta))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Delay", &v))) dur = v.uiVal * 10;
            else if (SUCCEEDED(meta->GetMetadataByName(L"/ANMF/FrameDuration", &v))) dur = (int)v.uiVal;
            PropVariantClear(&v);
        }
        if (dur < 20) dur = 100;
        p.cuadros.push_back(lienzo);
        p.duraciones.push_back(dur);
    }
    if (animado && p.cuadros.size() == 1) {
        p.bgra = std::move(p.cuadros[0]);
        p.cuadros.clear();
        p.duraciones.clear();
    }
    return p;
}

ComPtr<ID2D1Bitmap1> Gfx::subir(const Pixeles& p, size_t cuadro) {
    ComPtr<ID2D1Bitmap1> b;
    const std::vector<unsigned char>* px = &p.bgra;
    if (!p.cuadros.empty()) px = &p.cuadros[cuadro < p.cuadros.size() ? cuadro : 0];
    if (p.ancho == 0 || px->empty()) return b;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ctx->CreateBitmap(D2D1::SizeU(p.ancho, p.alto), px->data(), p.ancho * 4, &bp, &b);
    return b;
}
