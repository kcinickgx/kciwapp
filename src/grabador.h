// Grabacion de notas de voz por WASAPI: captura en modo compartido del
// microfono (el que se pida, o el que este por defecto), la mezcla a mono,
// la remuestrea a 16 kHz y la escribe como WAV PCM 16-bit. La conversion a
// Ogg/Opus la hace el server: con WAV alcanza de este lado.
#pragma once
#include <string>
#include <vector>

namespace grabador {

struct Dispositivo {
    std::wstring id;
    std::wstring nombre;
};

// entrada=true: microfonos (dispositivos de captura). entrada=false:
// parlantes/salidas. Solo dispositivos activos.
std::vector<Dispositivo> dispositivos(bool entrada);

// Arranca a grabar en un hilo aparte. id_o_vacio es el id de un microfono
// (de dispositivos(true)) o vacio para el que este por defecto. Devuelve
// false si no pudo abrir el dispositivo o crear el archivo (ya grabando
// cuenta como fallo: parar() primero). El WAV se escribe entero recien al
// llamar a parar() (o cuando el dispositivo se desconecta solo): esto es
// una nota de voz corta, no hace falta ir escribiendo a disco a medida que
// se graba.
bool empezar(const std::wstring& id_o_vacio, const std::wstring& ruta_wav);

// Corta la captura, remuestrea y mezcla lo grabado, escribe el WAV (con la
// cabecera ya bien puesta) y une el hilo. No hace nada si no se esta
// grabando.
void parar();

bool grabando();

// Segundos grabados hasta este momento (dominio del dispositivo, no del WAV
// de salida a 16kHz: es solo para mostrar un cronometro).
double segundos();

// Copia thread-safe de los picos RMS por cada ventana de 50ms desde que
// arranco la grabacion actual, 0..1, en orden. Para dibujar la forma de
// onda mientras se graba.
std::vector<float> niveles();

}  // namespace grabador
