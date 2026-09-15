// Ficha de info (contacto o grupo): tapa la conversacion, muestra foto,
// nombre, telefono/cantidad de miembros y, si es grupo, la lista de
// miembros (con admins primero). Se navega con una pila para volver atras
// cuando se entra a la ficha de un miembro desde un grupo.
#include "app.h"
#include "red.h"
#include "tema.h"

namespace {

const float BARRA_H = 60.0f;
const float FOTO_R = 70.0f;
const float MINI_R = 20.0f;
const float FILA_MIEMBRO_H = 56.0f;
const float PAD_TOP = 28.0f;

// Medidas del contenido, relativas al inicio del area scrolleable (justo
// debajo de la barra superior). Las usan tanto el dibujo como el hit-test,
// asi que van en un solo lugar.
float y_foto_c() { return PAD_TOP + FOTO_R; }          // centro vertical de la foto
float y_nombre_c() { return PAD_TOP + FOTO_R * 2 + 18.0f; }
float y_sub_c() { return y_nombre_c() + 28.0f; }
float y_seccion_c() { return y_sub_c() + 40.0f; }      // titulo "Members"
float y_filas_c() { return y_seccion_c() + 34.0f; }    // primera fila de miembro
float fila_miembro_c(int i) { return y_filas_c() + i * FILA_MIEMBRO_H; }

bool es_grupo(App* app, const std::string& jid) {
    if (const Chat* c = app->chat_de(jid)) return c->es_grupo;
    return jid.size() > 5 && jid.compare(jid.size() - 5, 5, "@g.us") == 0;
}

float alto_contenido_c(App* app, bool grupo) {
    if (!grupo) return y_sub_c() + 40.0f;
    if (app->cargando_miembros) return y_filas_c() + 40.0f;
    return y_filas_c() + app->miembros.size() * FILA_MIEMBRO_H + 12.0f;
}

// Pide la lista de miembros al server y la deja en app->miembros (admins
// primero, despues por nombre). Se fija que info_jid no haya cambiado
// antes de aplicar la respuesta (el usuario pudo haber navegado).
void cargar_miembros(App* app, const std::string& jid) {
    app->cargando_miembros = true;
    red::en_fondo([app, jid] {
        Respuesta r = red::obtener(L"/miembros?chat=" + ancho(jid));
        std::vector<App::Miembro> nuevos;
        if (r.ok()) {
            Json j = Json::parsear(r.cuerpo);
            for (size_t i = 0; i < j.largo(); i++)
                nuevos.push_back({j[i]["jid"].str(), j[i]["admin"].bul()});
        }
        red::en_ui([app, jid, nuevos] {
            if (app->info_jid != jid) return;
            app->miembros = nuevos;
            std::stable_sort(app->miembros.begin(), app->miembros.end(),
                              [app](const App::Miembro& a, const App::Miembro& b) {
                                  if (a.admin != b.admin) return a.admin;
                                  return app->nombre_de(a.jid) < app->nombre_de(b.jid);
                              });
            app->cargando_miembros = false;
            app->pedir_dibujo();
        });
    });
}

// Muestra la ficha de jid sin tocar la pila (la usan abrir_info, que apila
// antes de llamarla, y cerrar_info al volver atras).
void mostrar(App* app, const std::string& jid) {
    app->info_jid = jid;
    app->info_abierto = true;
    app->info_scroll = Desplazable();
    app->miembros.clear();
    app->cargando_miembros = false;
    if (es_grupo(app, jid)) cargar_miembros(app, jid);
    app->pedir_dibujo();
}

}  // namespace

void App::abrir_info(const std::string& jid) {
    if (info_abierto && info_jid != jid) info_pila.push_back(info_jid);
    mostrar(this, jid);
}

void App::cerrar_info() {
    if (!info_pila.empty()) {
        std::string anterior = info_pila.back();
        info_pila.pop_back();
        mostrar(this, anterior);
        return;
    }
    info_abierto = false;
    pedir_dibujo();
}

void App::dibujar_info() {
    if (!info_abierto) return;
    float x = x_conv(), W = w_conv();
    float top = alto_cabecera();
    bool grupo = es_grupo(this, info_jid);

    g.rect(x, top, W, g.alto - top, Color(BG_APP()));

    // Barra superior: volver (o cerrar, si no hay pila) y titulo.
    g.rect(x, top, W, BARRA_H, Color(BG_PANEL()));
    bool hay_vuelta = !info_pila.empty();
    g.renglon(hay_vuelta ? L"←" : L"✕", x + 20, top + BARRA_H / 2 - 10, 18, Color(TXT()));
    std::wstring titulo = grupo ? L"Group info" : L"Contact info";
    float tw = g.medir(titulo, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    g.renglon(titulo, x + (W - tw) / 2, top + BARRA_H / 2 - 11, 16, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD);

    float y0 = top + BARRA_H;
    float H = g.alto - y0;
    if (H <= 0) return;
    info_scroll.max = std::max(0.0f, alto_contenido_c(this, grupo) - H);
    info_scroll.limitar();

    g.recortar(x, y0, W, H);
    float base = y0 - info_scroll.pos;

    // Foto grande, centrada.
    float cx = x + W / 2, cy = base + y_foto_c();
    const Chat* c = chat_de(info_jid);
    bool tiene_foto = (c && c->tiene_foto) || (contactos.count(info_jid) && contactos[info_jid].tiene_foto);
    Imagen* foto = tiene_foto ? &imagen("foto:" + info_jid, L"/foto/" + ancho(info_jid), false) : nullptr;
    if (foto && foto->bmp) {
        g.bitmap_circular(foto->bmp.Get(), cx, cy, FOTO_R);
    } else {
        g.circulo(cx, cy, FOTO_R, Color(0x6b7c85));
        std::wstring nombre = nombre_de(info_jid);
        std::wstring inicial = nombre.empty() ? L"?" : nombre.substr(0, 1);
        float iw = g.medir(inicial, 48);
        g.renglon(inicial, cx - iw / 2, cy - 32, 48, Color(0xdfe5e7));
    }

    // Nombre.
    std::wstring nombre = nombre_de(info_jid);
    float nw = std::min(g.medir(nombre, 22, DWRITE_FONT_WEIGHT_SEMI_BOLD), W - 40);
    g.renglon(nombre, x + (W - nw) / 2, base + y_nombre_c(), 22, Color(TXT()), DWRITE_FONT_WEIGHT_SEMI_BOLD, W - 40);

    // Subtitulo: telefono (persona) o cantidad de miembros (grupo).
    std::wstring sub = grupo ? (std::to_wstring(miembros.size()) + L" members") : formatear_telefono(info_jid);
    float sw = g.medir(sub, 13);
    g.renglon(sub, x + (W - sw) / 2, base + y_sub_c(), 13, Color(TXT_DIM()));

    if (grupo) {
        g.renglon(L"Members", x + 20, base + y_seccion_c(), 13, Color(TXT_DIM()), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.linea(x + 20, base + y_seccion_c() + 22, x + W - 20, base + y_seccion_c() + 22, Color(BORDE()));
        if (cargando_miembros) {
            g.renglon(L"Loading...", x + 20, base + y_filas_c() + 14, 13, Color(TXT_DIM()));
        } else {
            for (size_t i = 0; i < miembros.size(); i++) {
                const Miembro& mm = miembros[i];
                float fy = base + fila_miembro_c((int)i);
                if (fy + FILA_MIEMBRO_H < y0) continue;
                if (fy > g.alto) break;
                bool bajo_mouse = mouse_x >= x && mouse_x < x + W && mouse_y >= fy && mouse_y < fy + FILA_MIEMBRO_H &&
                                   mouse_y >= y0;
                if (bajo_mouse) g.rect(x, fy, W, FILA_MIEMBRO_H, Color(BG_HOVER()));
                float mcx = x + 20 + MINI_R, mcy = fy + FILA_MIEMBRO_H / 2;
                bool m_tiene_foto = contactos.count(mm.jid) && contactos[mm.jid].tiene_foto;
                Imagen* mfoto = m_tiene_foto ? &imagen("foto:" + mm.jid, L"/foto/" + ancho(mm.jid), false) : nullptr;
                std::wstring nombre_m = mm.jid == mi_jid ? L"You" : nombre_de(mm.jid);
                if (mfoto && mfoto->bmp) {
                    g.bitmap_circular(mfoto->bmp.Get(), mcx, mcy, MINI_R);
                } else {
                    g.circulo(mcx, mcy, MINI_R, Color(0x6b7c85));
                    std::wstring ini = nombre_m.empty() ? L"?" : nombre_m.substr(0, 1);
                    float iw = g.medir(ini, 16);
                    g.renglon(ini, mcx - iw / 2, mcy - 11, 16, Color(0xdfe5e7));
                }
                float tx = x + 20 + MINI_R * 2 + 14;
                float dtx = 0;
                if (mm.admin) {
                    std::wstring et = L"admin";
                    float ew = g.medir(et, 12.5f);
                    dtx = ew + 16;
                    g.renglon(et, x + W - 20 - ew, fy + FILA_MIEMBRO_H / 2 - 8, 12.5f, Color(ACCENT()));
                }
                g.renglon(nombre_m, tx, fy + FILA_MIEMBRO_H / 2 - 9, 14.5f, Color(TXT()), DWRITE_FONT_WEIGHT_NORMAL,
                          W - tx - 20 - dtx);
                g.linea(tx, fy + FILA_MIEMBRO_H - 0.5f, x + W - 20, fy + FILA_MIEMBRO_H - 0.5f, Color(BORDE()));
            }
        }
    }
    g.destapar();
}

bool App::click_info(float x, float y) {
    if (!info_abierto) return false;
    if (x < x_conv()) return false;
    float top = alto_cabecera();
    if (y < top + BARRA_H) {
        cerrar_info();
        return true;
    }
    float y0 = top + BARRA_H;
    if (es_grupo(this, info_jid) && !cargando_miembros) {
        float base = y0 - info_scroll.pos;
        for (size_t i = 0; i < miembros.size(); i++) {
            float fy = base + fila_miembro_c((int)i);
            if (y >= fy && y < fy + FILA_MIEMBRO_H) {
                abrir_info(miembros[i].jid);
                return true;
            }
        }
    }
    return true;
}

bool App::rueda_info(float x, float y, float delta) {
    if (!info_abierto) return false;
    if (x < x_conv() || y < alto_cabecera()) return false;
    info_scroll.rodar(-delta / 120.0f * 3 * 40.0f);
    pedir_dibujo();
    return true;
}
