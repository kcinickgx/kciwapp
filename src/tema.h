// Ajustes del usuario (tema, colores, fondo, letras, notificaciones, audio)
// persistidos en <carpeta exe>\ajustes.json, y la paleta de colores vigente.
#pragma once
#include <functional>
#include <string>
#include <vector>

struct Paleta {
    unsigned bg_app = 0x111b21;       // fondo general y lista
    unsigned bg_panel = 0x202c33;     // cabeceras y pie
    unsigned bg_chat = 0x0b141a;      // fondo de la conversacion
    unsigned bg_hover = 0x202c33;
    unsigned bg_sel = 0x2a3942;       // chat elegido
    unsigned bg_campo = 0x2a3942;     // campo de texto
    unsigned burbuja_mia = 0x005c4b;
    unsigned burbuja_otra = 0x202c33;
    unsigned txt = 0xe9edef;
    unsigned txt_dim = 0x8696a0;
    unsigned acento = 0x00a884;
    unsigned tick_azul = 0x53bdeb;
    unsigned divisor = 0x182229;      // pastilla de fecha
    unsigned borde = 0x222d34;
    bool claro = false;               // tema claro: el fondo del chat se pinta distinto
};

struct Ajustes {
    std::string tema = "dark";        // dark, black, light, blue, orange, green, red, custom
    Paleta custom;                    // los colores del tema custom
    std::wstring fondo = L"whatsapp"; // "" sin fondo, "whatsapp" los garabatos, o un archivo de la carpeta fondos
    float letra_lista = 15.0f, letra_chat = 14.5f;
    bool notificaciones = true;
    bool llamadas_web = false;        // WhatsApp Web escondido para llamadas
    std::wstring entrada, salida;     // ids WASAPI ("" = el del sistema)
    int mensajes_por_chat = 200;      // cuantos se cargan al abrir un chat (0 = todos)
    int monitor_avisos = -1;          // indice del monitor para los avisos (-1 = el principal)
    int esquina_avisos = 0;           // 0 abajo-der, 1 arriba-der, 2 abajo-izq, 3 arriba-izq
    int segundos_aviso = 6;
    int volumen_video = 100;          // volumen del reproductor de video (0..100)
    // Donde quedo la ventana (rcNormalPosition, en pixeles fisicos); w = 0 = nunca guardada.
    int ventana_x = 0, ventana_y = 0, ventana_w = 0, ventana_h = 0;
    bool ventana_max = false;
};

namespace ajustes {

// Carga de <carpeta>\ajustes.json (o defaults) y guarda ahi.
void cargar(const std::wstring& carpeta_exe);
void guardar();

Ajustes& actual();
// Cambia algo y lo persiste; sube la revision para que la app se entere.
void cambiar(const std::function<void(Ajustes&)>& f);
unsigned revision();

// La paleta del tema vigente.
const Paleta& paleta();
Paleta paleta_de(const std::string& tema);
// Los presets, en orden, con su nombre para mostrar.
const std::vector<std::pair<std::string, std::wstring>>& temas();

// Archivos de imagen en <carpeta exe>\fondos\ (solo nombres).
std::vector<std::wstring> fondos_disponibles();
std::wstring carpeta_fondos();

// Colores como texto "#rrggbb" y vuelta.
std::string hex_de(unsigned c);
unsigned color_de_hex(const std::string& s, unsigned si_no);

}  // namespace ajustes

// Atajos para el codigo de dibujo: los colores del tema actual.
inline unsigned BG_APP() { return ajustes::paleta().bg_app; }
inline unsigned BG_PANEL() { return ajustes::paleta().bg_panel; }
inline unsigned BG_CHAT() { return ajustes::paleta().bg_chat; }
inline unsigned BG_HOVER() { return ajustes::paleta().bg_hover; }
inline unsigned BG_SEL() { return ajustes::paleta().bg_sel; }
inline unsigned BG_CAMPO() { return ajustes::paleta().bg_campo; }
inline unsigned BUBBLE_MIA() { return ajustes::paleta().burbuja_mia; }
inline unsigned BUBBLE_OTRA() { return ajustes::paleta().burbuja_otra; }
inline unsigned TXT() { return ajustes::paleta().txt; }
inline unsigned TXT_DIM() { return ajustes::paleta().txt_dim; }
inline unsigned ACCENT() { return ajustes::paleta().acento; }
inline unsigned TICK_AZUL() { return ajustes::paleta().tick_azul; }
inline unsigned DIVISOR() { return ajustes::paleta().divisor; }
inline unsigned BORDE() { return ajustes::paleta().borde; }
