// Traduccion de mensajes con un modelo local (llama.cpp + Qwen2.5-3B en
// translate\), como la transcripcion con whisper: opcional en el setup. El
// llama-server se levanta escondido la primera vez que hace falta y se queda
// mientras la app vive (job object); se le pide por HTTP en 127.0.0.1.
#include "app.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>

#include "cache.h"
#include "red.h"
#include "tema.h"

namespace {
HANDLE g_proceso = nullptr, g_job = nullptr;
int g_puerto = 0;
std::atomic<int> g_estado{0};  // 0 apagado, 1 arrancando, 2 listo, 3 fallo
std::mutex g_mu;

std::wstring carpeta_traductor() { return carpeta_exe() + L"\\translate"; }

std::wstring modelo_traductor() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((carpeta_traductor() + L"\\*.gguf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring r = carpeta_traductor() + L"\\" + fd.cFileName;
    FindClose(h);
    return r;
}

// Un puerto libre en 8600.. (por si hay dos kciwapp abiertos): se prueba
// escuchando un instante.
int puerto_libre() {
    for (int p = 8600; p < 8650; p++) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return 8600;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bool libre = bind(s, (sockaddr*)&a, sizeof a) == 0;
        closesocket(s);
        if (libre) return p;
    }
    return 8600;
}

// Lanza llama-server (una vez). Devuelve false si no hay traductor.
bool arrancar() {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_estado == 2 || g_estado == 1) return true;
    std::wstring exe = carpeta_traductor() + L"\\llama-server.exe", modelo = modelo_traductor();
    if (modelo.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_puerto = puerto_libre();
    std::wstring linea = L"\"" + exe + L"\" -m \"" + modelo + L"\" --host 127.0.0.1 --port " + std::to_wstring(g_puerto) +
                         L" -ngl 99 -c 4096 -np 1 --log-disable";
    if (!g_job) {
        g_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, linea.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        carpeta_traductor().c_str(), &si, &pi)) {
        red::registrar("traductor: no se pudo lanzar llama-server.exe");
        g_estado = 3;
        return false;
    }
    AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_proceso = pi.hProcess;
    g_estado = 1;
    red::registrar("traductor: llama-server en 127.0.0.1:" + std::to_string(g_puerto));
    return true;
}

// Espera a que /health diga ok (carga el modelo: unos segundos).
bool esperar_listo() {
    for (int i = 0; i < 600; i++) {
        if (g_estado == 2) return true;
        if (g_estado == 3 || g_estado == 0) return false;
        Respuesta r = red::pedir_a(L"127.0.0.1", g_puerto, L"GET", L"/health", "", L"", 2000);
        if (r.estado == 200) {
            g_estado = 2;
            return true;
        }
        if (g_proceso && WaitForSingleObject(g_proceso, 0) == WAIT_OBJECT_0) {
            g_estado = 3;
            red::registrar("traductor: llama-server se cerro");
            return false;
        }
        Sleep(250);
    }
    return false;
}

// Nombre del idioma para el prompt.
std::string idioma_destino() {
    std::string s = angosto(ajustes::actual().idioma_traduccion);
    return s.empty() ? "English" : s;
}
}  // namespace

bool App::traductor_disponible() const {
    static int cache = -1;
    if (cache < 0) cache = (!modelo_traductor().empty() && GetFileAttributesW((carpeta_traductor() + L"\\llama-server.exe").c_str()) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    return cache == 1;
}

// Pide la traduccion del texto del mensaje (o de su transcripcion, si es
// un audio) y la deja debajo del texto en la burbuja, guardada en la cache.
void App::traducir(int i) {
    if (i < 0 || i >= (int)mensajes.size()) return;
    Mensaje& m = mensajes[i];
    std::wstring texto = m.texto.empty() ? m.texto_oculto : m.texto;
    if (texto.empty() || traduciendo.count(m.id)) return;
    if (!arrancar()) {
        aviso_estado = L"Translation is not installed (translate\\ folder)";
        pedir_dibujo();
        return;
    }
    traduciendo.insert(m.id);
    aviso_estado = g_estado == 2 ? L"Translating..." : L"Loading the translation model...";
    pedir_dibujo();
    std::string chat = m.chat, id = m.id, original = angosto(texto), idioma = idioma_destino();
    m.traduccion.clear();  // si habia una a otro idioma, se reemplaza
    red::en_fondo([this, chat, id, original, idioma] {
        std::wstring error;
        std::string traduccion;
        if (!esperar_listo()) error = L"The translation model did not start";
        else {
            std::string sistema = "You are a translator. Translate the user's message into " + idioma +
                                  ". Reply with the translation only: no quotes, no notes, no explanations. Keep emojis, line breaks and names as they are. It is an informal chat message: translate idioms and slang by meaning and keep the casual tone. If the message is already in " + idioma + ", reply with it unchanged.";
            std::string cuerpo = "{\"messages\":[{\"role\":\"system\",\"content\":" + json_texto(sistema) + "},{\"role\":\"user\",\"content\":" + json_texto(original) + "}],\"temperature\":0,\"max_tokens\":1024}";
            Respuesta r = red::pedir_a(L"127.0.0.1", g_puerto, L"POST", L"/v1/chat/completions", cuerpo, L"application/json", 120000);
            if (r.estado != 200) error = L"Translation failed (" + std::to_wstring(r.estado) + L")";
            else {
                Json j = Json::parsear(r.cuerpo);
                traduccion = j["choices"][(size_t)0]["message"]["content"].str();
                // Sin espacios ni comillas de mas.
                while (!traduccion.empty() && (traduccion.back() == ' ' || traduccion.back() == '\n' || traduccion.back() == '\r')) traduccion.pop_back();
                while (!traduccion.empty() && (traduccion.front() == ' ' || traduccion.front() == '\n')) traduccion.erase(traduccion.begin());
                if (traduccion.size() > 2 && traduccion.front() == '"' && traduccion.back() == '"') traduccion = traduccion.substr(1, traduccion.size() - 2);
                if (traduccion.empty()) error = L"Empty translation";
            }
        }
        if (error.empty()) cache::guardar_traduccion(chat, id, traduccion, idioma);
        red::en_ui([this, chat, id, traduccion, error, idioma] {
            traduciendo.erase(id);
            aviso_estado = error;
            for (size_t k = 0; k < mensajes.size(); k++) {
                Mensaje& x = mensajes[k];
                if (x.chat != chat || x.id != id) continue;
                if (error.empty()) {
                    x.traduccion = ancho(traduccion);
                    x.traduccion_idioma = ancho(idioma);
                    x.traduccion_oculta = false;
                    rearmar_mensaje((int)k);
                }
                break;
            }
            pedir_dibujo();
        });
    });
}

bool App::boton_traducir_en(int i, float ym, float x, float y) const {
    if (i < 0 || i >= (int)vistas.size() || i >= (int)mensajes.size() || msg_bajo_mouse != i || seleccionando) return false;
    const Mensaje& m = mensajes[i];
    if (m.borrado || (m.texto.empty() && m.texto_oculto.empty()) || !traductor_disponible()) return false;
    const VistaMensaje& v = vistas[i];
    float cx = m.propio ? x_conv() + v.bx - 22 - 34 : x_conv() + v.bx + v.bw + 22 + 34, cy = ym + v.by + v.bh / 2;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 15 * 15;
}

// El iconito: traduce si no esta, si no muestra/esconde.
void App::click_traducir(int i) {
    if (i < 0 || i >= (int)mensajes.size()) return;
    if (mensajes[i].tiene_traduccion(ajustes::actual().idioma_traduccion)) alternar_traduccion(i);
    else traducir(i);
}

// Muestra/esconde la traduccion ya hecha.
void App::alternar_traduccion(int i) {
    if (i < 0 || i >= (int)mensajes.size() || !mensajes[i].tiene_traduccion(ajustes::actual().idioma_traduccion)) return;
    mensajes[i].traduccion_oculta = !mensajes[i].traduccion_oculta;
    rearmar_mensaje(i);
    pedir_dibujo();
}

// Rearma el layout de un mensaje cuyo alto cambio, sin mover lo que se ve.
void App::rearmar_mensaje(int i) {
    if (i < 0 || i >= (int)vistas.size()) return;
    float antes = vistas[i].alto;
    armar_vista(i);
    double d = vistas[i].alto - antes;
    recalcular_inicios();
    conv.max = std::max(0.0, alto_contenido() - (g.alto - alto_cabecera() - alto_pie));
    if (y_de((size_t)i) < alto_cabecera()) {
        conv.pos += d;
        conv.objetivo += d;
    }
}

void App::cerrar_traductor() {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_proceso) {
        TerminateProcess(g_proceso, 0);
        CloseHandle(g_proceso);
        g_proceso = nullptr;
    }
    if (g_job) {
        CloseHandle(g_job);
        g_job = nullptr;
    }
    g_estado = 0;
}
