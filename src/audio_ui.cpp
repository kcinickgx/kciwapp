// Audio en la interfaz: reproducir notas de voz/audios en la burbuja y
// grabar notas con el microfono (WASAPI -> WAV; el server lo pasa a Opus).
#include "app.h"
#include "grabador.h"
#include "red.h"
#include "tema.h"

namespace {

std::string leer_wav(const std::wstring& ruta) {
    std::string s;
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    LARGE_INTEGER tam;
    GetFileSizeEx(h, &tam);
    s.resize((size_t)tam.QuadPart);
    DWORD leido = 0;
    ReadFile(h, s.data(), (DWORD)s.size(), &leido, nullptr);
    CloseHandle(h);
    s.resize(leido);
    return s;
}

std::wstring mm_ss(double s) {
    int t = (int)s;
    return std::to_wstring(t / 60) + L":" + (t % 60 < 10 ? L"0" : L"") + std::to_wstring(t % 60);
}
}  // namespace

// Click en una tarjeta de audio: si es la que suena, pausa/sigue; si no,
// la baja (si hace falta) y la toca.
void App::reproducir_audio(int i) {
    const Mensaje& m = mensajes[i];
    cadena_audio_vista = false;
    if (reproduciendo_id == m.id) {
        if (reproductor.terminado()) {
            reproductor.ir_a(0);
            reproductor.reproducir();
        } else if (reproductor.pausado()) {
            reproductor.reproducir();
        } else {
            reproductor.pausar();
        }
        pedir_dibujo();
        return;
    }
    if (bajando_audio) return;
    bajando_audio = true;
    Mensaje copia = m;
    red::en_fondo([this, copia] {
        std::wstring ruta = bajar_media(copia);
        red::en_ui([this, copia, ruta] {
            bajando_audio = false;
            if (ruta.empty()) return;
            grab_escuchando = false;
            reproduciendo_id = copia.id;
            reproductor.abrir(ruta);
            reproductor.velocidad(velocidad_audio);
            reproductor.reproducir();
            pedir_dibujo();
        });
    });
}

// ---- grabacion ------------------------------------------------------------

void App::grabar_empezar() {
    if (chat_actual.empty() || grab != Grab::Nada) return;
    CreateDirectoryW((carpeta_exe() + L"\\datos").c_str(), nullptr);
    grab_ruta = carpeta_exe() + L"\\datos\\nota.wav";
    if (!grabador::empezar(ajustes::actual().entrada, grab_ruta)) {
        aviso_estado = L"No microphone available";
        pedir_dibujo();
        return;
    }
    grab = Grab::Grabando;
    grab_desde = GetTickCount64();
    mandar_presencia("recording");
    grab_niveles.clear();
    if (!reproduciendo_id.empty()) {
        reproductor.parar();
        reproduciendo_id.clear();
    }
    pedir_dibujo();
}

void App::grabar_parar() {
    if (grab != Grab::Grabando) return;
    grab_segundos = grabador::segundos();
    grab_niveles = grabador::niveles();
    grabador::parar();
    grab = Grab::Lista;
    mandar_presencia("");
    pedir_dibujo();
}

void App::grabar_cancelar() {
    if (grab == Grab::Grabando) {
        grabador::parar();
        mandar_presencia("");
    }
    if (grab_escuchando) {
        reproductor.parar();
        grab_escuchando = false;
    }
    grab = Grab::Nada;
    DeleteFileW(grab_ruta.c_str());
    campo.foco = true;
    pedir_dibujo();
}

void App::grabar_escuchar() {
    if (grab != Grab::Lista || !reproductor_ok) return;
    if (grab_escuchando) {
        if (reproductor.terminado()) {
            reproductor.ir_a(0);
            reproductor.reproducir();
        } else if (reproductor.pausado()) reproductor.reproducir();
        else reproductor.pausar();
    } else {
        reproduciendo_id.clear();
        grab_escuchando = true;
        reproductor.abrir(grab_ruta);
        reproductor.reproducir();
    }
    pedir_dibujo();
}

void App::grabar_mandar() {
    if (grab == Grab::Grabando) grabar_parar();
    if (grab != Grab::Lista) return;
    if (grab_escuchando) {
        reproductor.parar();
        grab_escuchando = false;
    }
    grab = Grab::Nada;
    std::string chat = chat_actual, cita = respondiendo ? respondiendo->id : "";
    respondiendo.reset();
    std::wstring ruta = grab_ruta;
    int segundos = (int)(grab_segundos + 0.5);
    aviso_estado = L"Sending voice message...";
    campo.foco = true;
    pedir_dibujo();
    red::en_fondo([this, chat, cita, ruta, segundos] {
        std::string datos = leer_wav(ruta);
        Respuesta r = red::mandar_archivo(L"/enviar", chat, "nota.wav", "audio/wav", datos, "", cita, "nota", segundos);
        Json j = Json::parsear(r.cuerpo);
        bool ok = r.ok();
        red::en_ui([this, j, ok] {
            aviso_estado.clear();
            if (!ok) aviso_estado = L"Could not send: " + ancho(j["error"].str("no response"));
            else agregar_mensaje(Mensaje::de_json(j));
            pedir_dibujo();
        });
    });
}

// La barra que reemplaza al campo mientras se graba o se escucha lo grabado:
// tacho | onda + tiempo | parar (grabando) o play + mandar (lista).
void App::dibujar_grabacion(float x, float yy, float W, float ch) {
    g.renglon(L"\U0001F5D1", x + 18, yy + ch / 2 - 12, 20, Color(0xf15c6d));
    float bx = x + 52, bw = W - 52 - 60;
    g.rect_redondo(bx, yy, bw, ch, 8, Color(BG_CAMPO()));
    std::vector<float> niveles = grab == Grab::Grabando ? grabador::niveles() : grab_niveles;
    double seg = grab == Grab::Grabando ? grabador::segundos() : grab_segundos;
    float tx = bx + 12;
    if (grab == Grab::Lista) {
        // Play/pausa de la vista previa.
        g.circulo(bx + 22, yy + ch / 2, 14, Color(ACCENT()));
        if (grab_escuchando && !reproductor.pausado() && !reproductor.terminado()) {
            g.rect(bx + 17, yy + ch / 2 - 6, 4, 12, Color(0x111b21));
            g.rect(bx + 23, yy + ch / 2 - 6, 4, 12, Color(0x111b21));
        } else {
            g.triangulo(bx + 17, yy + ch / 2 - 7, bx + 29, yy + ch / 2, bx + 17, yy + ch / 2 + 7, Color(0x111b21));
        }
        tx = bx + 46;
    } else {
        // Puntito rojo que late.
        float a = 0.5f + 0.5f * sinf((ahora % 1000) / 1000.0f * 6.2832f);
        g.circulo(bx + 22, yy + ch / 2, 6, Color(0xf15c6d, 0.4f + 0.6f * a));
        tx = bx + 40;
    }
    std::wstring t = mm_ss(seg);
    if (grab == Grab::Lista && grab_escuchando) t = mm_ss(reproductor.posicion()) + L" / " + t;
    float tw = g.medir(t, 13);
    g.renglon(t, bx + bw - tw - 14, yy + ch / 2 - 9, 13, Color(TXT()));
    // La onda: una barrita por muestra, las ultimas que entren.
    float ox = tx, ow = bx + bw - tw - 30 - tx, paso = 3.0f;
    int cuantas = std::max(1, (int)(ow / paso));
    int desde = std::max(0, (int)niveles.size() - cuantas);
    for (int k = desde; k < (int)niveles.size(); k++) {
        float h = std::max(2.0f, std::min(1.0f, niveles[k] * 3.0f) * (ch - 16));
        g.rect_redondo(ox + (k - desde) * paso, yy + ch / 2 - h / 2, 2, h, 1, Color(ACCENT(), 0.9f));
    }
    if (grab == Grab::Grabando) {
        g.rect_redondo(x + W - 44, yy + ch / 2 - 10, 20, 20, 4, Color(0xf15c6d));
    } else {
        g.renglon(L"➤", x + W - 44, yy + ch / 2 - 12, 22, Color(ACCENT()));
    }
}

bool App::click_grabacion(float x, float y, float yy, float ch) {
    float W = w_conv();
    if (x < x_conv() + 52) {
        grabar_cancelar();
        return true;
    }
    if (x > x_conv() + W - 60) {
        if (grab == Grab::Grabando) grabar_parar();
        else grabar_mandar();
        return true;
    }
    if (grab == Grab::Lista && x < x_conv() + 52 + 40) grabar_escuchar();
    return true;
}

// ---- la tarjeta de audio --------------------------------------------------

// Geometria de la tarjeta: play a la izquierda, la onda en el medio (clickeable
// para adelantar), tiempo abajo y, mientras suena, el boton de velocidad.
namespace {
const float AUDIO_ONDA_X = 52.0f;   // donde arranca la onda
const float AUDIO_ONDA_DER = 14.0f; // margen derecho
const float AUDIO_VEL_W = 40.0f;    // el boton 1x/1.5x/2x
}  // namespace

// Termino el que sonaba: si el mensaje que sigue en el chat es otro audio,
// arranca solo. Se decide una sola vez por audio, en el momento en que
// termina: lo que llegue despues ya no cuenta.
void App::encadenar_audio() {
    if (reproduciendo_id.empty() || cadena_audio_vista || !reproductor.terminado()) return;
    cadena_audio_vista = true;
    for (size_t i = 0; i + 1 < mensajes.size(); i++) {
        if (mensajes[i].id != reproduciendo_id) continue;
        const Mensaje& n = mensajes[i + 1];
        if ((n.tipo == "audio" || n.tipo == "nota") && n.media && !n.borrado) reproducir_audio((int)i + 1);
        return;
    }
}

void App::dibujar_audio(int i, float cx, float cy, float w, float h) {
    const Mensaje& m = mensajes[i];
    bool suena = reproduciendo_id == m.id;
    bool nota = m.tipo == "nota";
    g.rect_redondo(cx, cy, w, h, 6, Color(0x000000, 0.18f));
    // Play / pausa
    float pcx = cx + 24, pcy = cy + h / 2;
    g.circulo(pcx, pcy, 17, Color(ACCENT()));
    if (suena && !reproductor.pausado() && !reproductor.terminado()) {
        g.rect(pcx - 6, pcy - 7, 4, 14, Color(0x111b21));
        g.rect(pcx + 2, pcy - 7, 4, 14, Color(0x111b21));
    } else {
        g.triangulo(pcx - 5, pcy - 7.5f, pcx + 8, pcy, pcx - 5, pcy + 7.5f, Color(0x111b21));
    }
    // La onda: 64 barras de WhatsApp, o una plana si no hay (audio comun).
    float ox = cx + AUDIO_ONDA_X, ow = w - AUDIO_ONDA_X - AUDIO_ONDA_DER - AUDIO_VEL_W - 8;
    float oy = cy + 12, oh = 24;
    double d = suena ? reproductor.duracion() : (double)m.media->segundos;
    double pos = suena ? reproductor.posicion() : 0;
    float f = d > 0 ? (float)std::clamp(pos / d, 0.0, 1.0) : 0;
    int barras = 64;
    float paso = ow / barras;
    for (int k = 0; k < barras; k++) {
        float nivel;
        if (m.media->onda.size() >= 64) nivel = m.media->onda[k] / 100.0f;
        else nivel = nota ? 0.3f : 0.18f + 0.1f * ((k * 7) % 5);
        float bh = std::max(3.0f, nivel * oh);
        bool pasado = (k + 0.5f) / barras <= f;
        g.rect_redondo(ox + k * paso + paso * 0.2f, oy + (oh - bh) / 2, std::max(1.5f, paso * 0.6f), bh, 1,
                       Color(pasado ? ACCENT() : (nota ? TXT_DIM() : TXT_DIM()), pasado ? 1.0f : 0.6f));
    }
    if (suena) g.circulo(ox + ow * f, oy + oh / 2, 5, Color(ACCENT()));
    // Tiempo: lo que va (si suena) o el largo.
    int seg = (int)(suena ? pos : (double)m.media->segundos);
    std::wstring t = std::to_wstring(seg / 60) + L":" + (seg % 60 < 10 ? L"0" : L"") + std::to_wstring(seg % 60);
    g.renglon(t, ox, cy + h - 17, 11, Color(TXT_DIM()));
    // Y el largo total a la derecha, debajo del boton de velocidad.
    int total = (int)(suena && d > 0 ? d : (double)m.media->segundos);
    std::wstring tt = std::to_wstring(total / 60) + L":" + (total % 60 < 10 ? L"0" : L"") + std::to_wstring(total % 60);
    float ttw = g.medir(tt, 11);
    g.renglon(tt, ox + ow - ttw, cy + h - 17, 11, Color(TXT_DIM()));
    // Velocidad, siempre visible (es global).
    {
        float vx = cx + w - AUDIO_ONDA_DER - AUDIO_VEL_W, vy = cy + 12;
        g.rect_redondo(vx, vy, AUDIO_VEL_W, 24, 12, Color(TXT_DIM(), 0.35f));
        wchar_t buf[8];
        swprintf(buf, 8, velocidad_audio == 1.5 ? L"1.5x" : (velocidad_audio == 2.0 ? L"2x" : L"1x"));
        float tw = g.medir(buf, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(buf, vx + (AUDIO_VEL_W - tw) / 2, vy + 4, 12, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        // Transcripcion (whisper), debajo: sin texto = transcribir; con texto =
        // prendida (se puede esconder); escondida = apagada.
        if (whisper_disponible() || !m.texto.empty() || !m.texto_oculto.empty()) {
            bool en_curso = transcribiendo.count(m.id) > 0;
            bool visible = !m.texto.empty(), escondida = !visible && !m.texto_oculto.empty();
            float ty = cy + h - 22;
            Color fondo = visible ? Color(ACCENT(), 0.85f) : Color(TXT_DIM(), en_curso ? 0.2f : (escondida ? 0.25f : 0.35f));
            g.rect_redondo(vx, ty, AUDIO_VEL_W, 18, 9, fondo);
            const wchar_t* ic = en_curso ? L"\uE895" : L"\uED1E";  // Sync / Subtitles
            float iw = g.medir_fuente(L"Segoe MDL2 Assets", ic, 12);
            g.renglon_fuente(L"Segoe MDL2 Assets", ic, vx + (AUDIO_VEL_W - iw) / 2, ty + 3, 12,
                             Color(visible ? 0xffffff : TXT(), en_curso ? 0.6f : (escondida ? 0.7f : 1.0f)));
        }
    }
}

// Click dentro de la tarjeta (coordenadas relativas a ella).
void App::click_audio(int i, float rx, float ry, float w, float h) {
    // (rx, ry) son relativas a la tarjeta; para el arrastre guardamos la
    // onda en coordenadas absolutas.
    const Mensaje& m = mensajes[i];
    bool suena = reproduciendo_id == m.id;
    {
        float vx = w - AUDIO_ONDA_DER - AUDIO_VEL_W;
        if (rx >= vx && ry >= h - 24) {
            if (!m.texto.empty() || !m.texto_oculto.empty()) alternar_transcripcion(i);
            else if (whisper_disponible()) transcribir(i);
            return;
        }
        if (rx >= vx && ry >= 8 && ry <= 40) {
            velocidad_audio = velocidad_audio == 1.0 ? 1.5 : (velocidad_audio == 1.5 ? 2.0 : 1.0);
            ajustes::cambiar([v = velocidad_audio](Ajustes& a) { a.velocidad = v; });
            reproductor.velocidad(velocidad_audio);
            pedir_dibujo();
            return;
        }
    }
    if (suena) {
        float ox = AUDIO_ONDA_X, ow = w - AUDIO_ONDA_X - AUDIO_ONDA_DER - AUDIO_VEL_W - 8;
        if (rx >= ox && rx <= ox + ow && ry >= 6 && ry <= 42) {
            double d = reproductor.duracion();
            if (d > 0) {
                reproductor.ir_a(d * (rx - ox) / ow);
                if (reproductor.pausado()) reproductor.reproducir();
            }
            seek_msg = i;
            seek_x = mouse_x - rx + ox;
            seek_w = ow;
            pedir_dibujo();
            return;
        }
    }
    reproducir_audio(i);
}

// ---- transcripcion (whisper.cpp) --------------------------------------------

bool App::whisper_disponible() const {
    static int cache = -1;
    if (cache < 0) {
        std::wstring d = carpeta_exe() + L"\\whisper\\";
        cache = (GetFileAttributesW((d + L"whisper-cli.exe").c_str()) != INVALID_FILE_ATTRIBUTES &&
                 GetFileAttributesW((d + L"ggml-large-v3-turbo.bin").c_str()) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    }
    return cache == 1;
}

// Baja el WAV 16 kHz del server, corre whisper-cli escondido y manda el texto
// al server (que lo guarda como texto del mensaje y avisa por eventos).
void App::transcribir(int i) {
    if (i < 0 || i >= (int)mensajes.size() || !mensajes[i].media) return;
    const Mensaje& m = mensajes[i];
    if (transcribiendo.count(m.id)) return;
    transcribiendo.insert(m.id);
    pedir_dibujo();
    std::string chat = m.chat, id = m.id;
    long long mid = m.media->id;
    std::wstring base = carpeta_exe() + L"\\datos\\tmp";
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring wav = base + L"\\t" + std::to_wstring(mid) + L".wav";
    std::wstring salida = base + L"\\t" + std::to_wstring(mid);
    std::wstring exe = carpeta_exe() + L"\\whisper\\whisper-cli.exe";
    std::wstring modelo = carpeta_exe() + L"\\whisper\\ggml-large-v3-turbo.bin";
    red::en_fondo([this, chat, id, mid, wav, salida, exe, modelo] {
        std::wstring error;
        Respuesta r = red::obtener(L"/media/" + std::to_wstring(mid) + L"?wav=1", 120000);
        if (!r.ok()) error = L"Could not get the audio";
        else {
            HANDLE h = CreateFileW(wav.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) error = L"Cannot write temp file";
            else {
                DWORD e = 0;
                WriteFile(h, r.cuerpo.data(), (DWORD)r.cuerpo.size(), &e, nullptr);
                CloseHandle(h);
            }
        }
        std::string texto;
        if (error.empty()) {
            std::wstring linea = L"\"" + exe + L"\" -m \"" + modelo + L"\" -l auto -nt -np -f \"" + wav + L"\" -otxt -of \"" + salida + L"\"";
            STARTUPINFOW si{sizeof si};
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, linea.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                               (carpeta_exe() + L"\\whisper").c_str(), &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 600000);
                DWORD codigo = 1;
                GetExitCodeProcess(pi.hProcess, &codigo);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                HANDLE h = CreateFileW((salida + L".txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char buf[4096];
                    DWORD leido = 0;
                    while (ReadFile(h, buf, sizeof buf, &leido, nullptr) && leido > 0) texto.append(buf, leido);
                    CloseHandle(h);
                    DeleteFileW((salida + L".txt").c_str());
                } else error = L"whisper failed (" + std::to_wstring(codigo) + L")";
            } else error = L"Cannot run whisper-cli.exe";
            DeleteFileW(wav.c_str());
        }
        // Limpieza: sin saltos de linea sueltos ni espacios de mas.
        std::string limpio;
        for (char c : texto) {
            if (c == '\r') continue;
            if (c == '\n') c = ' ';
            if (c == ' ' && !limpio.empty() && limpio.back() == ' ') continue;
            limpio += c;
        }
        while (!limpio.empty() && limpio.back() == ' ') limpio.pop_back();
        while (!limpio.empty() && limpio.front() == ' ') limpio.erase(limpio.begin());
        if (error.empty()) {
            Respuesta p = red::mandar_json(L"/transcripcion", "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"texto\":" + json_texto(limpio) + "}");
            if (!p.ok()) error = L"Server rejected the transcript";
        }
        red::en_ui([this, id, error] {
            transcribiendo.erase(id);
            if (!error.empty()) aviso_estado = error;
            pedir_dibujo();
        });
    });
}

// Esconde o vuelve a mostrar la transcripcion de un audio (solo en memoria:
// al recargar el chat vuelve a verse).
void App::alternar_transcripcion(int i) {
    if (i < 0 || i >= (int)mensajes.size()) return;
    Mensaje& m = mensajes[i];
    if (!m.texto.empty()) {
        m.texto_oculto = m.texto;
        m.texto.clear();
    } else if (!m.texto_oculto.empty()) {
        m.texto = m.texto_oculto;
        m.texto_oculto.clear();
    } else return;
    float antes = vistas[i].alto;
    armar_vista(i);
    double d = vistas[i].alto - antes;
    recalcular_inicios();
    conv.max = std::max(0.0, alto_contenido() - (g.alto - alto_cabecera() - alto_pie));
    // Si el mensaje esta arriba de lo que se ve, la vista no se mueve.
    if (y_de((size_t)i) < alto_cabecera()) {
        conv.pos += d;
        conv.objetivo += d;
    }
    pedir_dibujo();
}
