#include "sonido.h"

#include <windows.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// El sonido se genera una sola vez, en memoria: una campanita corta y suave.
// No usa ningun archivo, asi es igual en toda maquina y controlamos que sea
// bien cortito y poco molesto.

namespace {

std::vector<char> g_wav;

#pragma pack(push, 1)
struct CabeceraWav {
    char riff[4];
    uint32_t tam;
    char wave[4];
    char fmt[4];
    uint32_t fmt_tam;
    uint16_t formato, canales;
    uint32_t frecuencia, bytes_seg;
    uint16_t bloque, bits;
    char data[4];
    uint32_t data_tam;
};
#pragma pack(pop)

void generar() {
    const int fs = 44100;
    const double dur = 0.11;  // 110 ms, casi nada
    const int n = (int)(fs * dur);
    std::vector<int16_t> pcm(n);
    const double PI = 3.14159265358979;
    for (int i = 0; i < n; i++) {
        double t = (double)i / fs;
        // Ataque suave de 4 ms (para que no haga "click") y caida exponencial
        // como una campana: el sonido dura poco y se apaga solo.
        double ataque = t < 0.004 ? t / 0.004 : 1.0;
        double env = ataque * std::exp(-t / 0.033);
        // Dos senoidales: la base (La 880 Hz) y una octava arriba mas floja,
        // le da un timbre de campanita en vez de un "beep" pelado.
        double s = 0.6 * std::sin(2 * PI * 880.0 * t) + 0.3 * std::sin(2 * PI * 1760.0 * t);
        double v = env * s * 0.35;  // volumen moderado
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        pcm[i] = (int16_t)(v * 32767);
    }
    uint32_t data_tam = (uint32_t)(pcm.size() * sizeof(int16_t));
    CabeceraWav h{};
    std::memcpy(h.riff, "RIFF", 4);
    std::memcpy(h.wave, "WAVE", 4);
    std::memcpy(h.fmt, "fmt ", 4);
    std::memcpy(h.data, "data", 4);
    h.fmt_tam = 16;
    h.formato = 1;  // PCM
    h.canales = 1;
    h.frecuencia = fs;
    h.bits = 16;
    h.bloque = h.canales * h.bits / 8;
    h.bytes_seg = h.frecuencia * h.bloque;
    h.data_tam = data_tam;
    h.tam = 36 + data_tam;
    g_wav.resize(sizeof(h) + data_tam);
    std::memcpy(g_wav.data(), &h, sizeof(h));
    std::memcpy(g_wav.data() + sizeof(h), pcm.data(), data_tam);
}

}  // namespace

namespace sonido {

void notificacion() {
    if (g_wav.empty()) generar();
    // SND_ASYNC no bloquea; el buffer es estatico, asi que sigue vivo mientras
    // suena. SND_NODEFAULT: si algo falla, que no meta el "ding" de Windows.
    PlaySoundA(g_wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

}  // namespace sonido
