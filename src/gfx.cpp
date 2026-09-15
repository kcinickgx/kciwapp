#include "gfx.h"

#include <shlwapi.h>

#include <cmath>

bool Gfx::iniciar(HWND h) {
    hwnd = h;
    dpi = (float)GetDpiForWindow(h);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL niveles[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                   D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11DeviceContext> d3dctx;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, niveles, 4, D3D11_SDK_VERSION,
                                   &d3d, nullptr, &d3dctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, niveles, 4, D3D11_SDK_VERSION, &d3d,
                               nullptr, &d3dctx);
        if (FAILED(hr)) return false;
    }
    ComPtr<IDXGIDevice1> dxgi;
    d3d.As(&dxgi);
    ComPtr<IDXGIAdapter> adaptador;
    dxgi->GetAdapter(&adaptador);
    ComPtr<IDXGIFactory2> fabrica_dxgi;
    adaptador->GetParent(IID_PPV_ARGS(&fabrica_dxgi));

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
    if (FAILED(hr)) return false;
    swap1.As(&swap);
    swap->SetMaximumFrameLatency(1);
    espera_frame = swap->GetFrameLatencyWaitableObject();
    fabrica_dxgi->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D2D1_FACTORY_OPTIONS opciones = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opciones, &fabrica);
    if (FAILED(hr)) return false;
    hr = fabrica->CreateDevice(dxgi.Get(), &dispositivo);
    if (FAILED(hr)) return false;
    hr = dispositivo->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
    if (FAILED(hr)) return false;
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), &dwrite);
    if (FAILED(hr)) return false;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;

    redimensionar();
    ctx->CreateSolidColorBrush(Color(0x53bdeb).d2d(), &pincel_enlace);
    return true;
}

void Gfx::redimensionar() {
    RECT r;
    GetClientRect(hwnd, &r);
    UINT px_ancho = r.right - r.left, px_alto = r.bottom - r.top;
    if (px_ancho == 0 || px_alto == 0) return;
    dpi = (float)GetDpiForWindow(hwnd);
    ancho = px_ancho * 96.0f / dpi;
    alto = px_alto * 96.0f / dpi;
    ctx->SetTarget(nullptr);
    destino.Reset();
    swap->ResizeBuffers(0, px_ancho, px_alto, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    ComPtr<IDXGISurface> superficie;
    swap->GetBuffer(0, IID_PPV_ARGS(&superficie));
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    ctx->CreateBitmapFromDxgiSurface(superficie.Get(), &bp, &destino);
    ctx->SetTarget(destino.Get());
    ctx->SetDpi(dpi, dpi);
}

void Gfx::empezar_frame() {
    if (!destino) redimensionar();
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    transformacion = D2D1::Matrix3x2F::Identity();
    capas_usadas = 0;
}

void Gfx::terminar_frame() {
    HRESULT hr = ctx->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        destino.Reset();
        return;
    }
    DXGI_PRESENT_PARAMETERS pp = {};
    swap->Present1(1, 0, &pp);
}

ID2D1SolidColorBrush* Gfx::pincel(Color c) {
    auto& p = pinceles[c.clave()];
    if (!p) ctx->CreateSolidColorBrush(c.d2d(), &p);
    return p.Get();
}

IDWriteTextFormat* Gfx::formato(float tamano, DWRITE_FONT_WEIGHT peso, const wchar_t* fuente) {
    std::wstring clave = fuente + std::to_wstring((int)(tamano * 10)) + L"/" + std::to_wstring((int)peso);
    std::lock_guard<std::mutex> candado(formatos_mu);
    auto& f = formatos[clave];
    if (!f) {
        dwrite->CreateTextFormat(fuente, nullptr, peso, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, tamano,
                                 L"es-AR", &f);
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
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
