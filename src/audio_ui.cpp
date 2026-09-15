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
    if (!grabador::empezar(L"", grab_ruta)) {
        aviso_estado = L"No microphone available";
        pedir_dibujo();
        return;
    }
    grab = Grab::Grabando;
    grab_desde = GetTickCount64();
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
    pedir_dibujo();
}

void App::grabar_cancelar() {
    if (grab == Grab::Grabando) grabador::parar();
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
            g.renglon(L"▶", bx + 15, yy + ch / 2 - 9, 14, Color(0x111b21));
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
