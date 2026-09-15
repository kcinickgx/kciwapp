#include "archivos.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <map>

using Microsoft::WRL::ComPtr;

namespace archivos {
namespace {

// RAII chico para CoInitializeEx/CoUninitialize: cada funcion que toca COM
// (dialogos con IFileDialog, WIC) se banca a si misma en cualquier hilo,
// este o no ya inicializado por quien la llama (los llamados son
// contabilizados, asi que siempre da lo mismo).
struct IniciadorCom {
    HRESULT hr;
    IniciadorCom() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~IniciadorCom() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

ComPtr<IWICImagingFactory> fabrica_wic() {
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    return wic;
}

// Codifica una imagen ya decodificada como PNG.
std::string codificar_png(IWICImagingFactory* wic, IWICBitmapSource* fuente) {
    if (!wic || !fuente) return "";
    ComPtr<IWICFormatConverter> convertidor;
    if (FAILED(wic->CreateFormatConverter(&convertidor))) return "";
    if (FAILED(convertidor->Initialize(fuente, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom)))
        return "";

    ComPtr<IStream> flujo;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &flujo))) return "";
    ComPtr<IWICBitmapEncoder> codificador;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &codificador))) return "";
    if (FAILED(codificador->Initialize(flujo.Get(), WICBitmapEncoderNoCache))) return "";
    ComPtr<IWICBitmapFrameEncode> cuadro;
    if (FAILED(codificador->CreateNewFrame(&cuadro, nullptr))) return "";
    if (FAILED(cuadro->Initialize(nullptr))) return "";
    UINT ancho = 0, alto = 0;
    convertidor->GetSize(&ancho, &alto);
    cuadro->SetSize(ancho, alto);
    WICPixelFormatGUID formato = GUID_WICPixelFormat32bppBGRA;
    cuadro->SetPixelFormat(&formato);
    if (FAILED(cuadro->WriteSource(convertidor.Get(), nullptr))) return "";
    if (FAILED(cuadro->Commit())) return "";
    if (FAILED(codificador->Commit())) return "";

    STATSTG estado{};
    flujo->Stat(&estado, STATFLAG_NONAME);
    ULONG tam = (ULONG)estado.cbSize.QuadPart;
    LARGE_INTEGER cero{};
    flujo->Seek(cero, STREAM_SEEK_SET, nullptr);
    std::string r(tam, '\0');
    ULONG leido = 0;
    flujo->Read(r.data(), tam, &leido);
    r.resize(leido);
    return r;
}

// Decodifica bytes de un archivo de imagen completo (BMP/PNG/JPG/... con su
// encabezado) y lo vuelve a codificar como PNG.
std::string bytes_a_png(const void* datos, size_t tam) {
    ComPtr<IWICImagingFactory> wic = fabrica_wic();
    if (!wic) return "";
    ComPtr<IStream> flujo;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &flujo))) return "";
    ULONG escrito = 0;
    flujo->Write(datos, (ULONG)tam, &escrito);
    LARGE_INTEGER cero{};
    flujo->Seek(cero, STREAM_SEEK_SET, nullptr);
    ComPtr<IWICBitmapDecoder> decodificador;
    if (FAILED(wic->CreateDecoderFromStream(flujo.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decodificador)))
        return "";
    ComPtr<IWICBitmapFrameDecode> cuadro;
    if (FAILED(decodificador->GetFrame(0, &cuadro))) return "";
    return codificar_png(wic.Get(), cuadro.Get());
}

// El CF_DIB/CF_DIBV5 del portapapeles es un BITMAPINFOHEADER (o mas grande)
// seguido de la paleta y los pixeles: un .bmp completo salvo por los 14
// bytes del BITMAPFILEHEADER. Se lo agregamos para poder decodificarlo con
// el decoder de BMP de WIC.
std::string dib_a_bmp(const void* datos, size_t tam) {
    if (tam <= sizeof(BITMAPINFOHEADER)) return "";
    const BITMAPINFOHEADER* info = (const BITMAPINFOHEADER*)datos;
    DWORD tam_paleta = 0;
    if (info->biBitCount != 0 && info->biBitCount <= 8) {
        DWORD colores = info->biClrUsed ? info->biClrUsed : (1u << info->biBitCount);
        tam_paleta = colores * sizeof(RGBQUAD);
    } else if (info->biCompression == BI_BITFIELDS) {
        tam_paleta = 3 * sizeof(DWORD);
    }
    BITMAPFILEHEADER encabezado{};
    encabezado.bfType = 0x4D42;  // "BM"
    encabezado.bfOffBits = (DWORD)(sizeof(BITMAPFILEHEADER) + info->biSize + tam_paleta);
    encabezado.bfSize = (DWORD)(sizeof(BITMAPFILEHEADER) + tam);
    std::string bmp;
    bmp.reserve(sizeof encabezado + tam);
    bmp.append((const char*)&encabezado, sizeof encabezado);
    bmp.append((const char*)datos, tam);
    return bmp;
}

}  // namespace

std::wstring elegir_archivo(HWND duenio) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = duenio;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrFilter = L"All files\0*.*\0";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return L"";
    return buf;
}

std::wstring elegir_carpeta(HWND duenio) {
    IniciadorCom com;
    std::wstring resultado;
    ComPtr<IFileDialog> dialogo;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialogo))))
        return resultado;
    DWORD opciones = 0;
    dialogo->GetOptions(&opciones);
    dialogo->SetOptions(opciones | FOS_PICKFOLDERS);
    if (FAILED(dialogo->Show(duenio))) return resultado;
    ComPtr<IShellItem> item;
    if (FAILED(dialogo->GetResult(&item))) return resultado;
    wchar_t* ruta = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &ruta))) {
        resultado = ruta;
        CoTaskMemFree(ruta);
    }
    return resultado;
}

std::wstring guardar_como(HWND duenio, const std::wstring& nombre_sugerido) {
    wchar_t buf[MAX_PATH] = L"";
    wcsncpy_s(buf, nombre_sugerido.c_str(), _TRUNCATE);
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = duenio;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrFilter = L"All files\0*.*\0";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return L"";
    return buf;
}

std::string leer_todo(const std::wstring& ruta) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string r;
    char buf[65536];
    DWORD leido = 0;
    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) r.append(buf, leido);
    CloseHandle(h);
    return r;
}

bool escribir_todo(const std::wstring& ruta, const std::string& datos) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD escrito = 0;
    bool ok = WriteFile(h, datos.data(), (DWORD)datos.size(), &escrito, nullptr) && escrito == datos.size();
    CloseHandle(h);
    return ok;
}

std::string mime_de(const std::wstring& ruta) {
    size_t p = ruta.find_last_of(L'.');
    if (p == std::wstring::npos) return "application/octet-stream";
    std::wstring ext = ruta.substr(p + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    static const std::map<std::wstring, std::string> tabla = {
        {L"jpg", "image/jpeg"},
        {L"jpeg", "image/jpeg"},
        {L"png", "image/png"},
        {L"gif", "image/gif"},
        {L"webp", "image/webp"},
        {L"bmp", "image/bmp"},
        {L"ico", "image/x-icon"},
        {L"mp4", "video/mp4"},
        {L"mov", "video/quicktime"},
        {L"avi", "video/x-msvideo"},
        {L"mkv", "video/x-matroska"},
        {L"webm", "video/webm"},
        {L"mp3", "audio/mpeg"},
        {L"m4a", "audio/mp4"},
        {L"ogg", "audio/ogg"},
        {L"opus", "audio/opus"},
        {L"wav", "audio/wav"},
        {L"aac", "audio/aac"},
        {L"pdf", "application/pdf"},
        {L"txt", "text/plain"},
        {L"zip", "application/zip"},
        {L"rar", "application/x-rar-compressed"},
        {L"7z", "application/x-7z-compressed"},
        {L"doc", "application/msword"},
        {L"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {L"xls", "application/vnd.ms-excel"},
        {L"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {L"ppt", "application/vnd.ms-powerpoint"},
        {L"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {L"csv", "text/csv"},
        {L"json", "application/json"},
        {L"html", "text/html"},
        {L"htm", "text/html"},
        {L"xml", "application/xml"},
    };
    auto it = tabla.find(ext);
    return it == tabla.end() ? "application/octet-stream" : it->second;
}

std::vector<std::wstring> archivos_del_portapapeles() {
    std::vector<std::wstring> r;
    if (!OpenClipboard(nullptr)) return r;
    HANDLE h = GetClipboardData(CF_HDROP);
    if (h) {
        HDROP hdrop = (HDROP)h;
        UINT n = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(hdrop, i, nullptr, 0);
            std::wstring ruta(len, L'\0');
            DragQueryFileW(hdrop, i, ruta.data(), len + 1);
            r.push_back(std::move(ruta));
        }
    }
    CloseClipboard();
    return r;
}

std::wstring texto_del_portapapeles() {
    std::wstring r;
    if (!OpenClipboard(nullptr)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t* datos = (wchar_t*)GlobalLock(h);
        if (datos) {
            r = datos;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return r;
}

bool portapapeles_tiene_imagen() {
    if (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5)) return true;
    if (!IsClipboardFormatAvailable(CF_HDROP)) return false;
    auto lista = archivos_del_portapapeles();
    return !lista.empty() && mime_de(lista[0]).rfind("image/", 0) == 0;
}

std::string imagen_del_portapapeles_png() {
    IniciadorCom com;
    std::string png;
    if (OpenClipboard(nullptr)) {
        HANDLE h = GetClipboardData(CF_DIBV5);
        if (!h) h = GetClipboardData(CF_DIB);
        if (h) {
            SIZE_T tam = GlobalSize(h);
            void* datos = GlobalLock(h);
            if (datos) {
                std::string bmp = dib_a_bmp(datos, tam);
                GlobalUnlock(h);
                if (!bmp.empty()) png = bytes_a_png(bmp.data(), bmp.size());
            }
        }
        CloseClipboard();
    }
    if (!png.empty()) return png;

    for (const auto& ruta : archivos_del_portapapeles()) {
        if (mime_de(ruta).rfind("image/", 0) != 0) continue;
        std::string bytes = leer_todo(ruta);
        if (bytes.empty()) continue;
        png = bytes_a_png(bytes.data(), bytes.size());
        if (!png.empty()) return png;
    }
    return "";
}

void abrir_con_sistema(const std::wstring& ruta) {
    ShellExecuteW(nullptr, L"open", ruta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void mostrar_en_explorador(const std::wstring& ruta) {
    std::wstring args = L"/select,\"" + ruta + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace archivos
