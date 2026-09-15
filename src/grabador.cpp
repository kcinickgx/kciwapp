#include "grabador.h"

#include <windows.h>

#include <mmdeviceapi.h>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <objbase.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace grabador {
namespace {

// RAII chico para CoInitializeEx/CoUninitialize (los llamados de COM se
// cuentan, asi que da lo mismo si el hilo ya estaba inicializado o no).
struct IniciadorCom {
    HRESULT hr;
    explicit IniciadorCom(DWORD modelo) : hr(CoInitializeEx(nullptr, modelo)) {}
    ~IniciadorCom() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

// GUID de KSDATAFORMAT_SUBTYPE_IEEE_FLOAT copiado a mano para no tener que
// arrastrar ks.h/ksmedia.h (y su orden de includes) solo por esto.
const GUID k_subformato_float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

const int k_frecuencia_salida = 16000;
const int k_muestras_por_ventana = k_frecuencia_salida / 20;  // 50ms a 16kHz

std::thread g_hilo;
std::atomic<bool> g_grabando{false};
std::atomic<bool> g_debe_parar{false};
std::atomic<double> g_segundos{0.0};
std::mutex g_mu_niveles;
std::vector<float> g_niveles;

void agregar_nivel(float pico) {
    std::lock_guard<std::mutex> lk(g_mu_niveles);
    g_niveles.push_back(pico);
}

// Escribe un WAV PCM mono 16-bit con cabecera de 44 bytes. Todo de una,
// porque ya tenemos las muestras completas en memoria (nota de voz corta).
bool escribir_wav(const std::wstring& ruta, const std::vector<int16_t>& muestras, int frecuencia) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    uint32_t bytes_datos = (uint32_t)(muestras.size() * sizeof(int16_t));
    uint32_t byte_rate = (uint32_t)frecuencia * 2;  // mono, 16-bit
    uint16_t block_align = 2;
    uint16_t bits_por_muestra = 16;
    uint16_t canales = 1;
    uint16_t formato_pcm = 1;
    uint32_t tam_fmt = 16;
    uint32_t tam_riff = 36 + bytes_datos;

    bool ok = true;
    DWORD escrito = 0;
    auto escribir = [&](const void* datos, DWORD n) {
        ok = ok && WriteFile(h, datos, n, &escrito, nullptr) && escrito == n;
    };
    escribir("RIFF", 4);
    escribir(&tam_riff, 4);
    escribir("WAVE", 4);
    escribir("fmt ", 4);
    escribir(&tam_fmt, 4);
    escribir(&formato_pcm, 2);
    escribir(&canales, 2);
    escribir(&frecuencia, 4);
    escribir(&byte_rate, 4);
    escribir(&block_align, 2);
    escribir(&bits_por_muestra, 2);
    escribir("data", 4);
    escribir(&bytes_datos, 4);
    if (ok && !muestras.empty()) escribir(muestras.data(), bytes_datos);

    CloseHandle(h);
    return ok;
}

// Remuestreo lineal simple de "entrada" (mono, a frecuencia_in) a 16kHz,
// devuelto ya como int16 con clamp.
std::vector<int16_t> remuestrear_a_16k(const std::vector<float>& entrada, int frecuencia_in) {
    std::vector<int16_t> salida;
    if (entrada.empty() || frecuencia_in <= 0) return salida;
    if (frecuencia_in == k_frecuencia_salida) {
        salida.reserve(entrada.size());
        for (float m : entrada) {
            float c = m < -1.0f ? -1.0f : (m > 1.0f ? 1.0f : m);
            salida.push_back((int16_t)lround(c * 32767.0f));
        }
        return salida;
    }

    double paso = (double)frecuencia_in / (double)k_frecuencia_salida;
    double duracion_muestras = (double)entrada.size();
    size_t n_salida = (size_t)(duracion_muestras / paso);
    salida.reserve(n_salida);
    double pos = 0.0;
    for (size_t i = 0; i < n_salida; i++) {
        size_t base = (size_t)pos;
        double frac = pos - (double)base;
        float a = entrada[base];
        float b = (base + 1 < entrada.size()) ? entrada[base + 1] : a;
        float m = a + (b - a) * (float)frac;
        float c = m < -1.0f ? -1.0f : (m > 1.0f ? 1.0f : m);
        salida.push_back((int16_t)lround(c * 32767.0f));
        pos += paso;
    }
    return salida;
}

// Cuerpo del hilo de captura. "promesa" avisa a empezar() si pudo abrir el
// dispositivo (true) o no (false); una vez resuelta, empezar() ya devolvio
// y esta funcion sigue sola hasta que parar() pida el corte.
void hilo_captura(std::wstring id, std::wstring ruta_wav, std::promise<bool> promesa) {
    IniciadorCom com(COINIT_MULTITHREADED);
    bool prometido = false;
    auto fallar = [&]() {
        if (!prometido) {
            promesa.set_value(false);
            prometido = true;
        }
    };

    ComPtr<IMMDeviceEnumerator> enumerador;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerador)))) {
        fallar();
        return;
    }

    ComPtr<IMMDevice> dispositivo;
    HRESULT hr = id.empty() ? enumerador->GetDefaultAudioEndpoint(eCapture, eCommunications, &dispositivo)
                             : enumerador->GetDevice(id.c_str(), &dispositivo);
    if (FAILED(hr)) {
        fallar();
        return;
    }

    ComPtr<IAudioClient> cliente;
    if (FAILED(dispositivo->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)cliente.GetAddressOf()))) {
        fallar();
        return;
    }

    WAVEFORMATEX* formato = nullptr;
    if (FAILED(cliente->GetMixFormat(&formato))) {
        fallar();
        return;
    }

    bool es_float = formato->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (formato->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(formato);
        es_float = IsEqualGUID(ext->SubFormat, k_subformato_float) != 0;
    }
    int canales = formato->nChannels;
    int frecuencia_in = formato->nSamplesPerSec;
    int bits = formato->wBitsPerSample;

    const REFERENCE_TIME duracion_buffer = 10'000'000;  // 1s, modo compartido
    hr = cliente->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, duracion_buffer, 0, formato, nullptr);
    CoTaskMemFree(formato);
    if (FAILED(hr)) {
        fallar();
        return;
    }

    ComPtr<IAudioCaptureClient> captura;
    if (FAILED(cliente->GetService(__uuidof(IAudioCaptureClient), (void**)captura.GetAddressOf()))) {
        fallar();
        return;
    }

    if (FAILED(cliente->Start())) {
        fallar();
        return;
    }

    // A partir de aca ya pudimos abrir todo: empezar() puede devolver true.
    g_grabando = true;
    promesa.set_value(true);
    prometido = true;

    std::vector<float> entrada_mono;
    uint64_t muestras_totales = 0;

    double acumulador_cuadrado = 0.0;
    int muestras_en_ventana = 0;

    while (!g_debe_parar.load()) {
        UINT32 tam_paquete = 0;
        if (FAILED(captura->GetNextPacketSize(&tam_paquete))) break;
        if (tam_paquete == 0) {
            Sleep(10);
            continue;
        }
        while (tam_paquete != 0) {
            BYTE* datos = nullptr;
            UINT32 cuadros = 0;
            DWORD banderas = 0;
            if (FAILED(captura->GetBuffer(&datos, &cuadros, &banderas, nullptr, nullptr))) {
                tam_paquete = 0;
                break;
            }

            for (UINT32 i = 0; i < cuadros; i++) {
                float mono = 0.0f;
                if (!(banderas & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    if (es_float) {
                        const float* m = reinterpret_cast<const float*>(datos) + (size_t)i * canales;
                        for (int c = 0; c < canales; c++) mono += m[c];
                    } else if (bits == 16) {
                        const int16_t* m = reinterpret_cast<const int16_t*>(datos) + (size_t)i * canales;
                        for (int c = 0; c < canales; c++) mono += m[c] / 32768.0f;
                    } else if (bits == 32) {
                        const int32_t* m = reinterpret_cast<const int32_t*>(datos) + (size_t)i * canales;
                        for (int c = 0; c < canales; c++) mono += m[c] / 2147483648.0f;
                    }
                    mono /= (float)canales;
                }
                entrada_mono.push_back(mono);
                muestras_totales++;

                acumulador_cuadrado += (double)mono * (double)mono;
                muestras_en_ventana++;
                if (muestras_en_ventana >= (frecuencia_in / 20)) {
                    float rms = (float)sqrt(acumulador_cuadrado / muestras_en_ventana);
                    agregar_nivel(rms > 1.0f ? 1.0f : rms);
                    acumulador_cuadrado = 0.0;
                    muestras_en_ventana = 0;
                }
            }

            g_segundos = (double)muestras_totales / (double)frecuencia_in;

            hr = captura->ReleaseBuffer(cuadros);
            if (FAILED(hr)) {
                tam_paquete = 0;
                break;
            }
            if (FAILED(captura->GetNextPacketSize(&tam_paquete))) break;
        }
    }

    cliente->Stop();

    std::vector<int16_t> salida = remuestrear_a_16k(entrada_mono, frecuencia_in);
    escribir_wav(ruta_wav, salida, k_frecuencia_salida);

    g_grabando = false;
}

}  // namespace

std::vector<Dispositivo> dispositivos(bool entrada) {
    std::vector<Dispositivo> r;
    IniciadorCom com(COINIT_APARTMENTTHREADED);

    ComPtr<IMMDeviceEnumerator> enumerador;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerador))))
        return r;

    ComPtr<IMMDeviceCollection> coleccion;
    EDataFlow flujo = entrada ? eCapture : eRender;
    if (FAILED(enumerador->EnumAudioEndpoints(flujo, DEVICE_STATE_ACTIVE, &coleccion))) return r;

    UINT n = 0;
    coleccion->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> dispositivo;
        if (FAILED(coleccion->Item(i, &dispositivo))) continue;

        Dispositivo d;
        LPWSTR id_crudo = nullptr;
        if (SUCCEEDED(dispositivo->GetId(&id_crudo))) {
            d.id = id_crudo;
            CoTaskMemFree(id_crudo);
        }

        ComPtr<IPropertyStore> propiedades;
        if (SUCCEEDED(dispositivo->OpenPropertyStore(STGM_READ, &propiedades))) {
            PROPVARIANT valor;
            PropVariantInit(&valor);
            if (SUCCEEDED(propiedades->GetValue(PKEY_Device_FriendlyName, &valor)) && valor.vt == VT_LPWSTR) {
                d.nombre = valor.pwszVal;
            }
            PropVariantClear(&valor);
        }
        r.push_back(std::move(d));
    }
    return r;
}

bool empezar(const std::wstring& id_o_vacio, const std::wstring& ruta_wav) {
    if (g_grabando.load()) return false;
    if (g_hilo.joinable()) g_hilo.join();  // por si quedo colgado de una grabacion anterior

    g_debe_parar = false;
    g_segundos = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_mu_niveles);
        g_niveles.clear();
    }

    std::promise<bool> promesa;
    std::future<bool> futuro = promesa.get_future();
    g_hilo = std::thread(hilo_captura, id_o_vacio, ruta_wav, std::move(promesa));
    bool ok = futuro.get();  // bloquea hasta que el hilo confirme si pudo abrir el microfono
    if (!ok && g_hilo.joinable()) g_hilo.join();
    return ok;
}

void parar() {
    if (!g_grabando.load()) return;
    g_debe_parar = true;
    if (g_hilo.joinable()) g_hilo.join();
}

bool grabando() { return g_grabando.load(); }

double segundos() { return g_segundos.load(); }

std::vector<float> niveles() {
    std::lock_guard<std::mutex> lk(g_mu_niveles);
    return g_niveles;
}

}  // namespace grabador
