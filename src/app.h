// La aplicacion: estado (chats, mensajes), layout cacheado, dibujo y entrada.
#pragma once
#include <algorithm>
#include <cmath>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "campo.h"
#include "gfx.h"
#include "json.h"
#include "reproductor.h"

#include <shellapi.h>

struct Media {
    long long id = 0;
    std::string mime, nombre;
    long long bytes = 0;
    int ancho = 0, alto = 0, segundos = 0, estado = 0;
    bool miniatura = false;
    std::vector<unsigned char> onda;  // 64 valores 0..100 (notas de voz)
};

struct Reaccion {
    std::string remitente;
    std::wstring emoji;
};

struct Mensaje {
    std::string id, chat, remitente;
    bool propio = false;
    long long ts = 0;
    std::string tipo;
    std::wstring texto;
    std::string cita_id, cita_remitente;
    std::wstring cita_texto;
    bool editado = false, borrado = false, reenviado = false;
    int estado = 0;
    std::optional<Media> media;
    std::vector<Reaccion> reacciones;

    static Mensaje de_json(const Json& j);
};

struct Chat {
    std::string jid;
    std::wstring nombre;
    bool es_grupo = false;
    bool tiene_foto = false;
    long long ultimo_ts = 0;
    int no_leidos = 0;
    bool archivado = false;
    std::optional<Mensaje> ultimo;

    static Chat de_json(const Json& j);
};

struct Contacto {
    std::wstring nombre;  // agenda, o push name, o telefono formateado
    bool tiene_foto = false;
};

// Scroll con animacion suave: la posicion persigue al objetivo.
// En double: un chat de 200k mensajes mide 10 millones de pixeles, y ahi
// un float ya no distingue fracciones de pixel (y acumula error al sumar).
struct Desplazable {
    double pos = 0, objetivo = 0, max = 0;
    void rodar(double delta) { objetivo = std::clamp(objetivo + delta, 0.0, max); }
    void ir(double a, bool ya = false) {
        objetivo = std::clamp(a, 0.0, max);
        if (ya) pos = objetivo;
    }
    void limitar() {
        max = std::max(max, 0.0);
        objetivo = std::clamp(objetivo, 0.0, max);
        pos = std::clamp(pos, 0.0, max);
    }
    // Devuelve si sigue moviendose.
    bool animar(float dt);
    bool quieto() const { return std::abs(objetivo - pos) < 0.05; }
};

// Una imagen en la GPU (o en camino).
struct Imagen {
    bool pedida = false, fallo = false;
    ComPtr<ID2D1Bitmap1> bmp;
    // Animaciones: los pixeles de cada cuadro y sus bitmaps a medida que se usan.
    Pixeles anim;
    std::vector<ComPtr<ID2D1Bitmap1>> cuadros;
    unsigned long long empezo = 0;
    ID2D1Bitmap1* cuadro_actual(Gfx& g, unsigned long long ahora);
};

// Layout de un mensaje, calculado una vez por ancho.
struct VistaMensaje {
    float alto = 0;         // alto total con margen
    float bx = 0, by = 0;   // burbuja relativa al renglon
    float bw = 0, bh = 0;
    float ancho_para = 0;   // el ancho de conversacion con el que se armo
    ComPtr<IDWriteTextLayout> texto;
    float tx = 0, ty = 0, tw = 0, th = 0;
    ComPtr<IDWriteTextLayout> nombre;
    float nh = 0;
    ComPtr<IDWriteTextLayout> cita;
    float ch = 0;
    float mx = 0, my = 0, mw = 0, mh = 0;   // media, relativo a la burbuja
    float hora_x = 0, hora_y = 0;
    bool hora_abajo = false;
    std::wstring hora_texto;
    bool divisor = false;   // lleva la pastilla de fecha arriba
    std::wstring divisor_texto;
    bool nuevo_bloque = false;
    float reacciones_alto = 0;
    // Links dentro del texto: rango en el layout y la URL.
    struct Enlace {
        size_t inicio, largo;
        std::wstring url;
    };
    std::vector<Enlace> enlaces;
};

// Un archivo listo para mandar (pegado, arrastrado o elegido).
struct Adjunto {
    std::wstring ruta;
    std::string datos, mime, nombre, tipo;
    ComPtr<ID2D1Bitmap1> vista;
    float w = 0, h = 0;
};

// Que se muestra en cada renglon de la lista de la izquierda.
struct ItemLista {
    enum Tipo { ChatItem, Titulo, Resultado } tipo;
    int idx;  // chat o resultado
    std::wstring titulo;
};

// Un renglon del menu contextual propio.
struct ItemMenu {
    std::wstring texto;
    int id = 0;
    const wchar_t* icono = nullptr;  // glifo de Segoe MDL2 Assets
    bool separador = false;
    bool habilitado = true;
    bool peligroso = false;          // en rojo (borrar)
};

struct App {
    Gfx g;
    HWND hwnd = nullptr;
    bool necesita_dibujar = true;
    unsigned long long ahora = 0;
    unsigned long long ultimo_frame = 0;

    // Datos
    std::vector<Chat> chats;
    std::map<std::string, Contacto> contactos;
    std::string mi_jid;
    std::string chat_actual;
    std::vector<Mensaje> mensajes;
    std::vector<VistaMensaje> vistas;
    // Los layouts se arman de abajo para arriba, de a tandas por frame:
    // [layout_pendiente, n) ya estan armados; lo de arriba, todavia no.
    size_t layout_pendiente = 0;
    int traza_frames = 0;  // F11: cuantos frames quedan por trazar
    unsigned long long abierto_en = 0;  // para medir cuanto tarda en armar todo
    // inicio[i] = alto acumulado hasta el mensaje i (desde el tope del
    // contenido), en double; inicio[n] es el total. Se rearma por frame.
    std::vector<double> inicio;
    void recalcular_inicios();
    // Donde cae en pantalla el renglon del mensaje i (su tope).
    float y_de(size_t i) const { return (float)(alto_cabecera() + 12 + (i < inicio.size() ? inicio[i] : 0) - conv.pos); }
    bool cargando_mensajes = false;
    bool cargando_chats = false, recarga_pendiente = false, escuchando = false;
    bool resync_pendiente = false;   // el log de eventos se perdio: completar cada chat al abrirlo
    // Los ultimos chats visitados quedan en memoria (mensajes ya parseados),
    // asi volver a uno es instantaneo. Los layouts se rearman (son lazy).
    struct ChatEnMemoria {
        std::vector<Mensaje> mensajes;
        bool hay_mas_viejos = true;
    };
    std::map<std::string, ChatEnMemoria> en_memoria;
    // Chats que el server reescribio (importacion / historia): al abrirlos
    // se vuelven a traer enteros, la cache no vale.
    std::set<std::string> chats_para_refrescar;
    // "historia" del chat abierto: se recarga una sola vez, 3 s despues del ultimo evento (timer 7).
    bool refresco_pendiente = false;
    void refrescar_chat_del_server(const std::string& jid);
    std::vector<std::string> en_memoria_orden;  // del mas viejo al mas reciente
    void recordar_chat();
    // Agrega mensajes viejos arriba sin mover la vista (layouts lazy).
    void anteponer(const std::vector<Mensaje>& viejos);
    bool hay_mas_viejos = true;
    long long seq_eventos = 0;
    bool conectado = false;
    std::wstring aviso_estado;

    // Vista
    float ancho_lista = 340.0f;
    Desplazable lista;
    Desplazable conv;
    int chat_bajo_mouse = -1;
    float mouse_x = 0, mouse_y = 0;
    bool arrastrando_barra = false, arrastrando_lista = false;
    float arrastre_origen = 0;
    Campo campo;
    Campo buscador;
    std::map<std::string, Imagen> imagenes;
    float letra_lista = 15.0f, letra_chat = 14.5f;
    std::vector<ItemLista> items;

    // Acciones sobre mensajes
    std::optional<Mensaje> respondiendo, editando;
    std::string resaltado_id;
    unsigned long long resaltado_desde = 0;
    int msg_bajo_mouse = -1;   // para la carita de reaccion al costado
    int reaccion_msg = -1;     // mensaje con la barra de reacciones abierta
    int emoji_para_reaccion = -1;  // el selector de emojis abierto para reaccionar a este
    int sel_msg = -1;          // mensaje con texto seleccionado
    size_t sel_a = 0, sel_b = 0;
    bool sel_arrastrando = false;
    float sel_x0 = 0, sel_y0 = 0;
    std::wstring enlace_pendiente;  // link bajo el mouse al apretar; se abre al soltar sin arrastrar
    bool cursor_mano = false;
    bool cursor_texto = false;  // I-beam sobre un campo de texto
    bool sobre_campo(float x, float y) const;
    bool reenviando = false;   // (viejo) eligiendo a que chat reenviar
    std::string reenviar_chat, reenviar_id;
    // Modo seleccion (reenviar varios) y el modal de destinatarios.
    bool seleccionando = false;
    std::set<std::string> seleccionados;   // ids
    bool modal_reenvio = false;
    Campo buscador_reenvio;
    std::set<std::string> destinos;        // jids elegidos en el modal
    Desplazable scroll_reenvio;
    // Rearmado de layouts por cambio de ancho, con debounce.
    float ancho_pendiente = 0;
    unsigned long long ultimo_resize = 0;
    std::map<std::string, std::wstring> borradores;
    std::map<std::string, std::optional<Adjunto>> borradores_adjunto;
    std::optional<Adjunto> adjunto;
    bool visor = false;        // foto (o video) a pantalla completa
    bool visor_video = false;
    HWND ventana_video = nullptr;  // ventana hija donde mpv dibuja
    std::string visor_clave;
    std::wstring visor_ruta;      // archivo local (video)
    std::wstring visor_url;       // ruta http de la foto, para volver a pedirla
    bool visor_animado = false;
    // Zoom y paneo del visor de fotos: 1 = entra en la ventana.
    float visor_zoom = 1.0f, visor_px = 0, visor_py = 0;
    bool visor_arrastrando = false;
    bool visor_seek = false;      // arrastrando la barra del video
    bool visor_vol = false;       // arrastrando el volumen del video
    bool video_mudo = false;
    float visor_ax = 0, visor_ay = 0, visor_mov = 0;
    // Busqueda
    std::vector<Mensaje> resultados;
    std::wstring ultima_busqueda;
    bool buscando = false;
    // La busqueda trae de a paginas (las mas nuevas primero); al llegar al
    // fondo de la lista se pide la siguiente hasta que no haya mas.
    bool busqueda_completa = false;
    // Busqueda dentro del chat abierto (buscar_chat.cpp).
    Campo buscador_chat;
    bool busca_chat_abierta = false;
    std::vector<Mensaje> res_chat;
    Desplazable scroll_res_chat;
    bool arrastrando_res_chat = false;
    int elegido_chat = -1;  // coincidencia elegida (-1 = se ve el panel)
    bool res_chat_completa = false, res_chat_buscando = false;
    std::wstring ultima_busqueda_chat;
    // "typing..." de los demas, por chat: quien, si escribe o graba, y hasta cuando vale.
    struct Escribiendo {
        std::string quien;
        bool grabando = false;
        unsigned long long hasta = 0;
    };
    std::map<std::string, Escribiendo> escribiendo;
    std::wstring texto_escribiendo(const std::string& chat);  // "" si nadie
    bool alguien_escribe(const std::string& chat) const;
    static constexpr float ESCRIBIENDO_H = 46.0f;  // la burbuja de "..." al final del chat
    void dibujar_burbuja_escribiendo(float y);
    void dibujar_pantalla_vacia();
    // Lo nuestro: que estado mandamos por ultima vez y cuando.
    std::string presencia_mandada;
    unsigned long long presencia_ts = 0;
    void mandar_presencia(const std::string& estado);  // "typing" | "recording" | ""
    void teclear_presencia();  // al cambiar el texto del campo
    // Llamadas (llamadas_ui.cpp): via WhatsApp Web escondido.
    bool llamada_activa = false, llamada_video = false, llamada_saliente = false;
    unsigned long long llamada_desde = 0;
    std::wstring llamada_con;
    std::string llamada_chat;
    std::map<std::string, std::pair<std::string, bool>> llamadas_entrantes;  // id -> (chat, video)
    bool llamadas_disponibles() const;
    void iniciar_llamada(bool video);
    void abrir_ventana_llamada();
    void atender_llamada(const std::string& chat, bool video);
    void llamada_cambio(bool en_curso);
    void llamada_conectada();
    // Vinculacion: el server no tiene sesion de WhatsApp; se le pide el QR
    // y se dibuja aca hasta que lo escaneen.
    bool sin_sesion = false;
    std::string qr_datos;
    ComPtr<ID2D1Bitmap1> qr_bmp;
    void tic_vinculacion();
    // "Cargar todo el chat" (boton de la cabecera cuando el preload es parcial),
    // con ventana de progreso.
    bool cargando_todo = false;
    long long todo_cargado = 0, todo_total = 0;
    unsigned long long todo_fin = 0;  // cuando termino (la ventanita queda un momento)
    void cargar_todo_el_chat();
    void dibujar_progreso_carga();
    void dibujar_vinculacion();
    void terminar_llamada_ui();
    void colgar_llamada();
    void ubicar_video_llamada();
    float alto_barra_llamada() const;
    void dibujar_barra_llamada();
    void dibujar_botones_llamada();
    bool click_llamada(float x, float y);
    bool clickeable_llamada(float x, float y) const;
    // Audio: un reproductor para notas de voz y audios, y la grabacion.
    Reproductor reproductor;
    bool reproductor_ok = false;
    std::string reproduciendo_id;   // mensaje que suena (vacio si ninguno)
    bool bajando_audio = false;
    double velocidad_audio = 1.0;   // 1x / 1.5x / 2x, global
    int seek_msg = -1;              // arrastrando la onda de este mensaje
    float seek_x = 0, seek_w = 0;   // la onda en coordenadas de ventana
    enum class Grab { Nada, Grabando, Lista } grab = Grab::Nada;
    unsigned long long grab_desde = 0;
    double grab_segundos = 0;
    std::wstring grab_ruta;
    std::vector<float> grab_niveles;
    bool grab_escuchando = false;   // la vista previa sonando

    // Selector de emojis (emoji_ui.cpp): un panel flotante arriba del pie.
    bool emojis_abierto = false;
    int emoji_categoria = -1;       // -1 = recientes
    Campo emoji_buscador;
    Desplazable emoji_scroll;
    int emoji_bajo_mouse = -1;

    // Panel de info del contacto o grupo (info_ui.cpp): tapa la conversacion.
    bool info_abierto = false;
    std::string info_jid;           // de quien
    std::vector<std::string> info_pila;  // para volver atras (grupo -> persona)
    struct Miembro { std::string jid; bool admin = false; };
    std::vector<Miembro> miembros;
    bool cargando_miembros = false;
    Desplazable info_scroll;

    // Ajustes: la revision que ya aplicamos, y el fondo del chat en la GPU.
    unsigned ajustes_aplicados = 0;
    ComPtr<ID2D1Bitmap1> fondo_bmp;
    ComPtr<ID2D1BitmapBrush> fondo_pincel;
    std::wstring fondo_cargado;

    void iniciar(HWND h);
    void dibujar();
    bool animando();
    void pedir_dibujo() { necesita_dibujar = true; }

    // Entrada
    void raton_mueve(float x, float y);
    void raton_abajo(float x, float y, bool shift);
    void raton_arriba(float x, float y);
    void raton_derecho(float x, float y);
    void doble_click(float x, float y);
    void rueda(float x, float y, float delta);
    void tecla(WPARAM vk, bool shift, bool ctrl);
    void caracter(wchar_t c);
    void redimensionado();
    void soltar_archivos(HDROP h);
    void escapar();

    // Datos
    void cargar_chats();
    void abrir_chat(const std::string& jid);
    void cargar_mas_viejos();
    void enviar_texto();
    void escuchar_eventos();
    // Devuelve si hay que recargar la lista de chats.
    bool aplicar_evento(const Json& e);
    // Marca leido en WhatsApp: los ids dados, o (sin ids) lo reciente del chat.
    void marcar_leido(const std::string& jid, const std::vector<std::string>& ids = {});
    void ventana_activada();

    // Menu contextual propio (menu_ui.cpp)
    bool menu_abierto = false;
    std::vector<ItemMenu> menu_items;
    std::function<void(int)> menu_accion;
    float menu_x = 0, menu_y = 0;
    void abrir_menu(std::vector<ItemMenu> items, float x, float y, std::function<void(int)> accion);
    void cerrar_menu();
    void dibujar_menu();
    bool click_menu(float x, float y);
    void menu_campo(float x, float y);
    // Acciones (acciones.cpp)
    void menu_contextual(int i);
    void responder(int i);
    void editar(int i);
    void borrar(int i);
    void reaccionar(int i, const std::wstring& emoji);
    // La carita al costado del mensaje y la barra de reacciones rapidas.
    void dibujar_reacciones_de(int i, float y);
    bool click_reacciones(float x, float y);
    void copiar_mensaje(int i);
    void reenviar(int i);
    void reenviar_a(const std::string& destino);
    // Seleccion multiple y reenvio (reenviar_ui.cpp)
    void empezar_seleccion(int i);
    void terminar_seleccion();
    void alternar_seleccion(int i);
    void dibujar_seleccion_barra();
    bool click_seleccion(float x, float y);
    void dibujar_modal_reenvio();
    bool click_modal_reenvio(float x, float y);
    bool rueda_modal_reenvio(float x, float y, float delta);
    void enviar_reenvio();
    void ir_a_mensaje(const std::string& chat, const std::string& id, long long ts);
    void buscar_ahora();
    void buscar_mas();
    void abrir_busqueda_chat();
    void cerrar_busqueda_chat();
    void buscar_en_chat();
    void buscar_mas_chat();
    void ir_a_coincidencia(int i);
    void mover_coincidencia(int dir);
    void dibujar_busqueda_cabecera(float tx, float x, float W, float H);
    void dibujar_resultados_chat();
    bool panel_resultados_chat() const;
    bool click_busqueda_chat(float x, float y, bool shift);
    bool clickeable_busqueda_chat(float x, float y) const;
    void adjuntar_archivo(const std::wstring& ruta);
    void adjuntar_datos(std::string datos, std::string mime, std::string nombre);
    void enviar_adjunto();
    void pegar();
    void abrir_media(int i);
    void abrir_visor(int i);
    void abrir_video(int i);
    void cerrar_visor();
    bool click_visor(float x, float y);
    void rueda_visor(float x, float y, float delta);
    // El rect donde se dibuja la foto del visor (con zoom y paneo).
    bool rect_visor(float& x, float& y, float& w, float& h);
    std::wstring bajar_media(const Mensaje& m);
    void copiar_media(int i);
    void elegir_archivo();
    void copiar_seleccion();
    int mensaje_en(float y, float* y_msg);
    bool en_texto(int i, float y_msg, float x, float y, size_t* indice);
    // Si en (x, y) hay algo que se pueda clickear (para el cursor de mano).
    bool sobre_clickeable(float x, float y);
    std::wstring enlace_en(int i, float y_msg, float x, float y);
    void agregar_mensaje(const Mensaje& m);
    // Emojis (emoji_ui.cpp). El panel se dibuja encima de todo, anclado
    // arriba-izquierda del pie; `click_emojis` devuelve si se comio el click.
    void dibujar_emojis();
    bool click_emojis(float x, float y);
    bool rueda_emojis(float x, float y, float delta);
    void elegir_emoji(const std::wstring& simbolo);
    // Info (info_ui.cpp): abre la ficha de un chat o persona.
    void abrir_info(const std::string& jid);
    void cerrar_info();
    void dibujar_info();
    bool click_info(float x, float y);
    bool rueda_info(float x, float y, float delta);
    // Ajustes (app.cpp): aplica lo que cambio en la ventana de settings.
    void aplicar_ajustes();
    void dibujar_fondo_chat(float x, float y, float w, float h);
    // Audio (audio_ui.cpp)
    void reproducir_audio(int i);
    void dibujar_audio(int i, float cx, float cy, float w, float h);
    void click_audio(int i, float rx, float ry, float w, float h);
    void grabar_empezar();
    void grabar_parar();
    void grabar_cancelar();
    void grabar_mandar();
    void grabar_escuchar();
    bool click_grabacion(float x, float y, float yy, float ch);
    void dibujar_grabacion(float x, float yy, float W, float ch);

    // Regiones (DIPs)
    float x_conv() const { return ancho_lista; }
    float w_conv() const { return g.ancho - ancho_lista; }
    static constexpr float CABECERA_H = 60.0f;
    // La cabecera mas la barra de llamada en curso (si hay).
    float alto_cabecera() const { return CABECERA_H + alto_barra_llamada(); }
    float top_lista() const { return 104.0f; }
    // Alto de una fila de la lista: acompana al tamano de letra.
    float fila_h() const { return std::round(letra_lista * 3.2f + 20.0f); }
    float alto_pie = 0;

    void dibujar_lista();
    void dibujar_conversacion();
    void dibujar_cabecera();
    void dibujar_pie();
    void dibujar_visor();
    void barra_scroll(const Desplazable& d, float x, float top, float H, double total, bool cerca, bool arrastrando);
    void agarrar_barra(Desplazable& d, float y, float top, float H, double total);
    void arrastrar_barra(Desplazable& d, float y, float top, float H, double total);
    void tildes(float x, float y, bool doble, Color c);
    void dibujar_mensaje(size_t i, float y);
    void armar_items();
    void armar_vistas();
    void armar_vista(size_t i);
    // El armado puro de una vista: solo lee lo que le pasan (sirve desde
    // hilos de fondo). `anterior` es el mensaje de arriba, o null.
    void armar_vista_core(const Mensaje& m, const Mensaje* anterior, bool es_grupo,
                          const std::function<std::wstring(const std::string&)>& nombre, float W, VistaMensaje& v);
    // Armado de fondo en varios hilos de lo pendiente; los resultados se
    // aplican en el hilo de UI de abajo hacia arriba.
    std::atomic<unsigned> layout_gen{0};
    std::map<size_t, std::vector<VistaMensaje>> tandas_listas;  // por indice final
    void lanzar_armado();
    void aplicar_tandas();
    // Arma hasta `cuantos` layouts pendientes (o hasta agotar `ms_max`);
    // devuelve el alto agregado arriba de lo visible.
    double avanzar_layouts(int cuantos, float ms_max);
    double alto_contenido() const;
    void bajar_al_final(bool ya);
    bool al_final() const;
    Imagen& imagen(const std::string& clave, const std::wstring& ruta, bool animado);
    std::wstring nombre_de(const std::string& jid);
    const Chat* chat_de(const std::string& jid) const;
    void ordenar_chats();
    int item_en(float y);
};

std::wstring formatear_hora(long long ts);
std::wstring formatear_dia(long long ts);
std::wstring formatear_telefono(const std::string& jid);
std::wstring nombre_tipo(const std::string& tipo);
// Un solo renglon (saltos -> espacios), para vistas previas.
std::wstring una_linea(std::wstring s);
std::wstring plano(const std::wstring& s);
std::wstring carpeta_exe();
Color color_de_nombre(const std::wstring& nombre);
