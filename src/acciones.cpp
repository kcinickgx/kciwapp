// Acciones sobre mensajes y chats: menu contextual, responder, editar,
// borrar, reaccionar, reenviar, buscar, adjuntar, visor de fotos.
#include "app.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "cache.h"
#include "red.h"
#include "tema.h"

namespace {


enum IdMenu {
    M_RESPONDER = 1, M_COPIAR, M_REENVIAR, M_EDITAR, M_BORRAR, M_ABRIR, M_GUARDAR, M_MOSTRAR,
    M_REACCION = 100  // + indice
};

const wchar_t* REACCIONES_RAPIDAS[] = {L"\U0001F44D", L"❤️", L"\U0001F602", L"\U0001F62E", L"\U0001F622", L"\U0001F64F"};

std::wstring extension_de(const std::string& mime, const std::string& nombre) {
    size_t p = nombre.find_last_of('.');
    if (p != std::string::npos && nombre.size() - p <= 6) return ancho(nombre.substr(p));
    std::string base = mime.substr(0, mime.find(';'));
    static const std::pair<const char*, const wchar_t*> tabla[] = {
        {"image/jpeg", L".jpg"}, {"image/png", L".png"}, {"image/webp", L".webp"}, {"image/gif", L".gif"},
        {"video/mp4", L".mp4"}, {"audio/ogg", L".ogg"}, {"audio/mpeg", L".mp3"}, {"audio/mp4", L".m4a"},
        {"application/pdf", L".pdf"}, {"text/plain", L".txt"},
    };
    for (auto& [m, e] : tabla)
        if (base == m) return e;
    return L".bin";
}

std::string mime_por_extension(const std::wstring& ruta) {
    std::wstring e = ruta.substr(ruta.find_last_of(L'.') == std::wstring::npos ? ruta.size() : ruta.find_last_of(L'.'));
    for (auto& c : e) c = towlower(c);
    static const std::pair<const wchar_t*, const char*> tabla[] = {
        {L".jpg", "image/jpeg"}, {L".jpeg", "image/jpeg"}, {L".png", "image/png"}, {L".webp", "image/webp"},
        {L".gif", "image/gif"}, {L".mp4", "video/mp4"}, {L".mov", "video/quicktime"}, {L".mp3", "audio/mpeg"},
        {L".m4a", "audio/mp4"}, {L".ogg", "audio/ogg"}, {L".opus", "audio/ogg"}, {L".wav", "audio/wav"},
        {L".pdf", "application/pdf"}, {L".txt", "text/plain"}, {L".zip", "application/zip"},
    };
    for (auto& [x, m] : tabla)
        if (e == x) return m;
    return "application/octet-stream";
}

std::string leer_archivo(const std::wstring& ruta) {
    std::string s;
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    LARGE_INTEGER tam;
    GetFileSizeEx(h, &tam);
    s.resize((size_t)tam.QuadPart);
    DWORD leido = 0;
    size_t total = 0;
    while (total < s.size() && ReadFile(h, s.data() + total, (DWORD)std::min<size_t>(1 << 20, s.size() - total), &leido, nullptr) && leido > 0)
        total += leido;
    CloseHandle(h);
    s.resize(total);
    return s;
}

bool escribir_archivo(const std::wstring& ruta, const std::string& datos) {
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD escrito = 0;
    size_t total = 0;
    while (total < datos.size() && WriteFile(h, datos.data() + total, (DWORD)std::min<size_t>(1 << 20, datos.size() - total), &escrito, nullptr))
        total += escrito;
    CloseHandle(h);
    return total == datos.size();
}

// La imagen del portapapeles como PNG (vacio si no hay).
std::string portapapeles_png(Gfx& g) {
    std::string r;
    if (!IsClipboardFormatAvailable(CF_BITMAP) || !OpenClipboard(nullptr)) return r;
    HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (hbm) {
        ComPtr<IWICBitmap> wb;
        if (SUCCEEDED(g.wic->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapIgnoreAlpha, &wb))) {
            ComPtr<IStream> flujo(SHCreateMemStream(nullptr, 0));
            ComPtr<IWICBitmapEncoder> enc;
            g.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
            if (enc && flujo && SUCCEEDED(enc->Initialize(flujo.Get(), WICBitmapEncoderNoCache))) {
                ComPtr<IWICBitmapFrameEncode> cuadro;
                enc->CreateNewFrame(&cuadro, nullptr);
                cuadro->Initialize(nullptr);
                if (SUCCEEDED(cuadro->WriteSource(wb.Get(), nullptr)) && SUCCEEDED(cuadro->Commit()) &&
                    SUCCEEDED(enc->Commit())) {
                    STATSTG st;
                    flujo->Stat(&st, STATFLAG_NONAME);
                    r.resize((size_t)st.cbSize.QuadPart);
                    LARGE_INTEGER cero = {};
                    flujo->Seek(cero, STREAM_SEEK_SET, nullptr);
                    ULONG leido = 0;
                    flujo->Read(r.data(), (ULONG)r.size(), &leido);
                    r.resize(leido);
                }
            }
        }
    }
    CloseClipboard();
    return r;
}

void al_portapapeles(const std::wstring& s) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

}  // namespace

std::wstring carpeta_exe() {
    wchar_t ruta[MAX_PATH];
    GetModuleFileNameW(nullptr, ruta, MAX_PATH);
    std::wstring r = ruta;
    size_t corte = r.find_last_of(L'\\');
    return corte == std::wstring::npos ? L"." : r.substr(0, corte);
}

// Sin acentos ni mayusculas, para comparar nombres y buscar.
std::wstring plano(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) {
        wchar_t b = towlower(c);
        static const wchar_t* de = L"áàäâãåéèëêíìïîóòöôõúùüûñçýÿ";
        static const wchar_t* a = L"aaaaaaeeeeiiiiooooouuuuncyy";
        const wchar_t* p = wcschr(de, b);
        if (p && b) b = a[p - de];
        r += b;
    }
    return r;
}

// ---- geometria ------------------------------------------------------------

// Que mensaje hay en la coordenada y de la conversacion; deja en y_msg el
// tope del renglon del mensaje.
int App::mensaje_en(float y, float* y_msg) {
    float yy = alto_cabecera() + 12 - conv.pos;
    for (size_t i = 0; i < vistas.size(); i++) {
        const VistaMensaje& v = vistas[i];
        if (y >= yy + v.by && y < yy + v.by + v.bh) {
            if (y_msg) *y_msg = yy;
            return (int)i;
        }
        yy += v.alto;
    }
    return -1;
}

bool App::en_texto(int i, float y_msg, float x, float y, size_t* indice) {
    const VistaMensaje& v = vistas[i];
    if (!v.texto) return false;
    float tx = x_conv() + v.bx + 9.0f, ty = y_msg + v.by + v.ty;
    BOOL final = FALSE, dentro = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    v.texto->HitTestPoint(x - tx, y - ty, &final, &dentro, &m);
    if (indice) *indice = std::min<size_t>(m.textPosition + (final ? m.length : 0), mensajes[i].texto.size());
    return x >= tx - 4 && x <= tx + v.tw + 4 && y >= ty && y <= ty + v.th;
}

// La URL del link bajo el punto (x, y) del mensaje i, o vacia.
std::wstring App::enlace_en(int i, float y_msg, float x, float y) {
    const VistaMensaje& v = vistas[i];
    if (!v.texto || v.enlaces.empty()) return L"";
    float tx = x_conv() + v.bx + 9.0f, ty = y_msg + v.by + v.ty;
    BOOL final = FALSE, dentro = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    v.texto->HitTestPoint(x - tx, y - ty, &final, &dentro, &m);
    if (!dentro) return L"";
    for (auto& e : v.enlaces)
        if (m.textPosition >= e.inicio && m.textPosition < e.inicio + e.largo) return e.url;
    return L"";
}

// ---- menu contextual ------------------------------------------------------

void App::raton_derecho(float x, float y) {
    if (x < ancho_lista || visor) return;
    float ym = 0;
    int i = mensaje_en(y, &ym);
    if (i < 0) return;
    menu_contextual(i);
}

void App::menu_contextual(int i) {
    const Mensaje& m = mensajes[i];
    HMENU menu = CreatePopupMenu();
    // Reacciones rapidas arriba, como en WhatsApp.
    HMENU reac = CreatePopupMenu();
    for (int k = 0; k < 6; k++) AppendMenuW(reac, MF_STRING, M_REACCION + k, REACCIONES_RAPIDAS[k]);
    AppendMenuW(reac, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(reac, MF_STRING, M_REACCION + 99, L"Remove reaction");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)reac, L"React");
    AppendMenuW(menu, MF_STRING, M_RESPONDER, L"Reply");
    if (!m.texto.empty() || sel_msg == i) AppendMenuW(menu, MF_STRING, M_COPIAR, L"Copy");
    if (!m.borrado) AppendMenuW(menu, MF_STRING, M_REENVIAR, L"Forward");
    if (m.propio && !m.borrado && !m.media && !m.texto.empty()) AppendMenuW(menu, MF_STRING, M_EDITAR, L"Edit");
    if (m.media && !m.borrado) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, M_ABRIR, L"Open");
        AppendMenuW(menu, MF_STRING, M_GUARDAR, L"Save as...");
        AppendMenuW(menu, MF_STRING, M_MOSTRAR, L"Show in folder");
    }
    if (!m.borrado) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, M_BORRAR, m.propio ? L"Delete for everyone" : L"Delete");
    }
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    campo.foco = true;
    pedir_dibujo();
    if (id >= M_REACCION) {
        int k = id - M_REACCION;
        reaccionar(i, k == 99 ? L"" : REACCIONES_RAPIDAS[k]);
        return;
    }
    switch (id) {
        case M_RESPONDER: responder(i); break;
        case M_COPIAR: copiar_mensaje(i); break;
        case M_REENVIAR: reenviar(i); break;
        case M_EDITAR: editar(i); break;
        case M_BORRAR: borrar(i); break;
        case M_ABRIR: abrir_media(i); break;
        case M_GUARDAR: {
            std::wstring origen = bajar_media(mensajes[i]);
            if (origen.empty()) break;
            wchar_t nombre[MAX_PATH] = L"";
            std::wstring sugerido = mensajes[i].media->nombre.empty()
                                        ? L"whatsapp" + extension_de(mensajes[i].media->mime, "")
                                        : ancho(mensajes[i].media->nombre);
            wcsncpy_s(nombre, sugerido.c_str(), MAX_PATH - 1);
            OPENFILENAMEW ofn = {sizeof ofn};
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = nombre;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn)) CopyFileW(origen.c_str(), nombre, FALSE);
            break;
        }
        case M_MOSTRAR: {
            std::wstring ruta = bajar_media(mensajes[i]);
            if (!ruta.empty()) ShellExecuteW(nullptr, nullptr, L"explorer.exe", (L"/select,\"" + ruta + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
    }
}

void App::responder(int i) {
    respondiendo = mensajes[i];
    editando.reset();
    campo.foco = true;
    pedir_dibujo();
}

void App::editar(int i) {
    editando = mensajes[i];
    respondiendo.reset();
    campo.poner(mensajes[i].texto);
    campo.foco = true;
    pedir_dibujo();
}

void App::borrar(int i) {
    std::string chat = mensajes[i].chat, id = mensajes[i].id;
    red::en_fondo([chat, id] {
        red::mandar_json(L"/borrar", "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + "}");
    });
}

void App::reaccionar(int i, const std::wstring& emoji) {
    std::string chat = mensajes[i].chat, id = mensajes[i].id, e = angosto(emoji);
    // Si ya tengo esa misma reaccion, se saca.
    for (auto& r : mensajes[i].reacciones)
        if (r.remitente == mi_jid && r.emoji == emoji) e = "";
    red::en_fondo([chat, id, e] {
        red::mandar_json(L"/reaccion", "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"emoji\":" + json_texto(e) + "}");
    });
}

void App::copiar_mensaje(int i) {
    if (sel_msg == i && sel_a != sel_b) {
        copiar_seleccion();
        return;
    }
    al_portapapeles(mensajes[i].texto);
}

void App::copiar_seleccion() {
    if (sel_msg < 0 || sel_msg >= (int)mensajes.size() || sel_a == sel_b) return;
    size_t a = std::min(sel_a, sel_b), b = std::max(sel_a, sel_b);
    al_portapapeles(mensajes[sel_msg].texto.substr(a, b - a));
}

void App::reenviar(int i) {
    reenviando = true;
    reenviar_chat = mensajes[i].chat;
    reenviar_id = mensajes[i].id;
    pedir_dibujo();
}

void App::reenviar_a(const std::string& destino) {
    reenviando = false;
    std::string chat = reenviar_chat, id = reenviar_id;
    red::en_fondo([this, chat, id, destino] {
        Respuesta r = red::mandar_json(L"/reenviar", "{\"chat\":" + json_texto(chat) + ",\"id\":" + json_texto(id) + ",\"destino\":" + json_texto(destino) + "}");
        Json j = Json::parsear(r.cuerpo);
        bool ok = r.ok();
        red::en_ui([this, j, ok] {
            if (!ok) aviso_estado = L"Could not forward: " + ancho(j["error"].str("no response"));
            else agregar_mensaje(Mensaje::de_json(j));
            pedir_dibujo();
        });
    });
    abrir_chat(destino);
}

// Mete un mensaje nuevo (propio recien mandado, o llegado por eventos) en
// el chat abierto, si corresponde, y actualiza la lista.
void App::agregar_mensaje(const Mensaje& m) {
    for (auto& c : chats)
        if (c.jid == m.chat) {
            c.ultimo = m;
            c.ultimo_ts = std::max(c.ultimo_ts, m.ts);
        }
    ordenar_chats();
    if (m.chat != chat_actual) return;
    for (auto& x : mensajes)
        if (x.id == m.id) return;
    bool abajo = al_final();
    mensajes.push_back(m);
    vistas.emplace_back();
    armar_vista(mensajes.size() - 1);
    conv.max = std::max(0.0f, alto_contenido() - (g.alto - alto_cabecera() - alto_pie));
    if (abajo || m.propio) bajar_al_final(false);
}

// ---- ir a un mensaje (desde la busqueda o una notificacion) ---------------

void App::ir_a_mensaje(const std::string& chat, const std::string& id, long long ts) {
    if (chat != chat_actual) {
        // Se abre el chat con una ventana de mensajes alrededor del pedido.
        borradores[chat_actual] = campo.texto;
        chat_actual = chat;
        mensajes.clear();
        vistas.clear();
        cargando_mensajes = true;
        hay_mas_viejos = true;
        conv = Desplazable();
        campo.poner(borradores[chat]);
        respondiendo.reset();
        editando.reset();
    }
    resaltado_id = id;
    resaltado_desde = GetTickCount64();
    // Si ya esta cargado, alcanza con scrollear.
    for (size_t i = 0; i < mensajes.size(); i++)
        if (mensajes[i].id == id) {
            float y = 12;
            for (size_t k = 0; k < i; k++) y += vistas[k].alto;
            float H = g.alto - alto_cabecera() - alto_pie;
            conv.ir(y - H / 2 + vistas[i].alto / 2, false);
            pedir_dibujo();
            return;
        }
    cargando_mensajes = true;
    std::string mio = chat;
    red::en_fondo([this, mio, id, ts] {
        Respuesta a = red::obtener(L"/mensajes?chat=" + ancho(mio) + L"&antes=" + std::to_wstring(ts + 1) + L"&limite=40");
        Respuesta d = red::obtener(L"/mensajes?chat=" + ancho(mio) + L"&desde=" + std::to_wstring(ts + 1) + L"&limite=40");
        Json ja = Json::parsear(a.cuerpo), jd = Json::parsear(d.cuerpo);
        red::en_ui([this, mio, id, ja, jd] {
            if (mio != chat_actual) return;
            cargando_mensajes = false;
            mensajes.clear();
            for (size_t i = 0; i < ja.largo(); i++) mensajes.push_back(Mensaje::de_json(ja[i]));
            for (size_t i = 0; i < jd.largo(); i++) {
                Mensaje m = Mensaje::de_json(jd[i]);
                bool repetido = false;
                for (auto& x : mensajes)
                    if (x.id == m.id) repetido = true;
                if (!repetido) mensajes.push_back(m);
            }
            hay_mas_viejos = ja.largo() >= 40;
            cache::guardar_mensajes(mensajes);
            armar_vistas();
            float H = g.alto - alto_cabecera() - alto_pie;
            conv.max = std::max(0.0f, alto_contenido() - H);
            float y = 12;
            for (size_t i = 0; i < mensajes.size(); i++) {
                if (mensajes[i].id == id) {
                    conv.ir(y - H / 2 + vistas[i].alto / 2, true);
                    break;
                }
                y += vistas[i].alto;
            }
            pedir_dibujo();
        });
    });
}

// ---- busqueda -------------------------------------------------------------

void App::buscar_ahora() {
    std::wstring q = buscador.texto;
    while (!q.empty() && q.back() == L' ') q.pop_back();
    if (q == ultima_busqueda) return;
    ultima_busqueda = q;
    if (q.size() < 2) {
        resultados.clear();
        pedir_dibujo();
        return;
    }
    buscando = true;
    std::string qq = angosto(q);
    // Escapado minimo para la URL.
    std::wstring url = L"/buscar?limite=40&q=";
    for (unsigned char c : qq) {
        if (isalnum(c)) url += (wchar_t)c;
        else {
            wchar_t buf[8];
            swprintf(buf, 8, L"%%%02X", c);
            url += buf;
        }
    }
    red::en_fondo([this, url, q] {
        Respuesta r = red::obtener(url);
        Json j = Json::parsear(r.cuerpo);
        red::en_ui([this, j, q] {
            if (q != ultima_busqueda) return;
            buscando = false;
            resultados.clear();
            for (size_t i = 0; i < j.largo(); i++) resultados.push_back(Mensaje::de_json(j[i]));
            lista.ir(0, true);
            pedir_dibujo();
        });
    });
}

// ---- adjuntos -------------------------------------------------------------

void App::adjuntar_archivo(const std::wstring& ruta) {
    std::string datos = leer_archivo(ruta);
    if (datos.empty()) return;
    size_t corte = ruta.find_last_of(L"\\/");
    std::wstring nombre = corte == std::wstring::npos ? ruta : ruta.substr(corte + 1);
    adjuntar_datos(std::move(datos), mime_por_extension(ruta), angosto(nombre));
    adjunto->ruta = ruta;
}

void App::adjuntar_datos(std::string datos, std::string mime, std::string nombre) {
    Adjunto a;
    a.datos = std::move(datos);
    a.mime = std::move(mime);
    a.nombre = std::move(nombre);
    if (a.mime.rfind("image/", 0) == 0) {
        a.tipo = a.mime == "image/webp" ? "figurita" : "imagen";
        Pixeles p = g.decodificar(a.datos, false);
        if (!p.vacio()) {
            a.vista = g.subir(p);
            a.w = (float)p.ancho;
            a.h = (float)p.alto;
        }
        // Un WebP pegado se manda como foto, no como sticker.
        if (a.tipo == "figurita") a.tipo = "imagen";
    } else if (a.mime.rfind("video/", 0) == 0) {
        a.tipo = "video";
    } else if (a.mime.rfind("audio/", 0) == 0) {
        a.tipo = "audio";
    } else {
        a.tipo = "documento";
    }
    adjunto = std::move(a);
    campo.foco = true;
    pedir_dibujo();
}

void App::enviar_adjunto() {
    if (!adjunto || chat_actual.empty()) return;
    Adjunto a = std::move(*adjunto);
    adjunto.reset();
    std::wstring t = campo.texto;
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\n')) t.pop_back();
    campo.poner(L"");
    std::string chat = chat_actual, cita = respondiendo ? respondiendo->id : "";
    respondiendo.reset();
    aviso_estado = L"Sending " + ancho(a.nombre) + L"...";
    pedir_dibujo();
    std::string texto = angosto(t);
    red::en_fondo([this, chat, a, texto, cita] {
        Respuesta r = red::mandar_archivo(L"/enviar", chat, a.nombre, a.mime, a.datos, texto, cita, a.tipo, 0);
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

void App::pegar() {
    // Primero una imagen; si no, archivos; si no, texto al campo.
    std::string png = portapapeles_png(g);
    if (!png.empty()) {
        adjuntar_datos(std::move(png), "image/png", "pasted.png");
        return;
    }
    if (IsClipboardFormatAvailable(CF_HDROP) && OpenClipboard(nullptr)) {
        HDROP h = (HDROP)GetClipboardData(CF_HDROP);
        std::wstring ruta;
        if (h) {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(h, 0, buf, MAX_PATH)) ruta = buf;
        }
        CloseClipboard();
        if (!ruta.empty()) {
            adjuntar_archivo(ruta);
            return;
        }
    }
    campo.pegar();
    pedir_dibujo();
}

void App::soltar_archivos(HDROP h) {
    wchar_t buf[MAX_PATH];
    if (DragQueryFileW(h, 0, buf, MAX_PATH)) adjuntar_archivo(buf);
    DragFinish(h);
}

void App::elegir_archivo() {
    wchar_t nombre[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = nombre;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) adjuntar_archivo(nombre);
    campo.foco = true;
    pedir_dibujo();
}

// ---- media ----------------------------------------------------------------

// Baja (si hace falta) el adjunto a datos\media y devuelve la ruta local.
std::wstring App::bajar_media(const Mensaje& m) {
    if (!m.media) return L"";
    std::wstring carpeta = carpeta_exe() + L"\\datos\\media";
    CreateDirectoryW((carpeta_exe() + L"\\datos").c_str(), nullptr);
    CreateDirectoryW(carpeta.c_str(), nullptr);
    std::wstring ruta = carpeta + L"\\" + std::to_wstring(m.media->id) + extension_de(m.media->mime, m.media->nombre);
    if (GetFileAttributesW(ruta.c_str()) != INVALID_FILE_ATTRIBUTES) return ruta;
    Respuesta r = red::obtener(L"/media/" + std::to_wstring(m.media->id), 300000);
    if (!r.ok() || r.cuerpo.empty()) {
        aviso_estado = r.estado == 410 ? L"That file is no longer available on WhatsApp" : L"Could not download the file";
        pedir_dibujo();
        return L"";
    }
    escribir_archivo(ruta, r.cuerpo);
    return ruta;
}

void App::abrir_media(int i) {
    const Mensaje& m = mensajes[i];
    if (!m.media) return;
    if (m.tipo == "imagen" || m.tipo == "figurita") {
        abrir_visor(i);
        return;
    }
    if ((m.tipo == "audio" || m.tipo == "nota") && reproductor_ok) {
        reproducir_audio(i);
        return;
    }
    if ((m.tipo == "video" || m.tipo == "gif") && reproductor_ok) {
        abrir_video(i);
        return;
    }
    // Lo demas se abre con lo que tenga Windows (video, audio, documentos).
    Mensaje copia = m;
    red::en_fondo([this, copia] {
        std::wstring ruta = bajar_media(copia);
        if (!ruta.empty()) ShellExecuteW(nullptr, L"open", ruta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
}

void App::abrir_visor(int i) {
    const Mensaje& m = mensajes[i];
    if (!m.media) return;
    visor = true;
    visor_zoom = 1.0f;
    visor_px = visor_py = 0;
    visor_arrastrando = false;
    visor_clave = "media:" + std::to_string(m.media->id);
    visor_url = L"/media/" + std::to_wstring(m.media->id);
    visor_animado = m.tipo == "figurita";
    imagen(visor_clave, visor_url, visor_animado);
    pedir_dibujo();
}

bool App::rect_visor(float& x, float& y, float& w, float& h) {
    Imagen& im = imagen(visor_clave, visor_url, visor_animado);
    ID2D1Bitmap1* b = im.cuadro_actual(g, ahora);
    if (!b) return false;
    D2D1_SIZE_F t = b->GetSize();
    float esc = std::min((g.ancho - 80) / t.width, (g.alto - 80) / t.height);
    esc = std::min(esc, 2.0f) * visor_zoom;
    w = t.width * esc;
    h = t.height * esc;
    x = (g.ancho - w) / 2 + visor_px;
    y = (g.alto - h) / 2 + visor_py;
    return true;
}

// Rueda sobre el visor: zoom alrededor del punto del mouse.
void App::rueda_visor(float x, float y, float delta) {
    if (visor_video) return;
    float ix, iy, iw, ih;
    if (!rect_visor(ix, iy, iw, ih)) return;
    float factor = delta > 0 ? 1.15f : 1 / 1.15f;
    float nuevo = std::clamp(visor_zoom * factor, 1.0f, 12.0f);
    factor = nuevo / visor_zoom;
    // El punto bajo el mouse se queda quieto.
    float cx = g.ancho / 2 + visor_px, cy = g.alto / 2 + visor_py;
    visor_px += (cx - x) * (factor - 1);
    visor_py += (cy - y) * (factor - 1);
    visor_zoom = nuevo;
    if (visor_zoom == 1.0f) visor_px = visor_py = 0;
    pedir_dibujo();
}

// Rect del video dentro del visor, en DIPs.
static void rect_video(const App& a, float& x, float& y, float& w, float& h) {
    x = 40;
    y = 40;
    w = a.g.ancho - 80;
    h = a.g.alto - 150;
}

void App::abrir_video(int i) {
    Mensaje copia = mensajes[i];
    aviso_estado = L"Loading video...";
    pedir_dibujo();
    red::en_fondo([this, copia] {
        std::wstring ruta = bajar_media(copia);
        red::en_ui([this, ruta] {
            aviso_estado.clear();
            if (ruta.empty()) return;
            if (!ventana_video) {
                WNDCLASSW wc = {};
                wc.lpfnWndProc = DefWindowProcW;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = L"kciwapp2-video";
                wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
                RegisterClassW(&wc);
                ventana_video = CreateWindowExW(0, L"kciwapp2-video", L"", WS_CHILD, 0, 0, 10, 10, hwnd, nullptr,
                                               wc.hInstance, nullptr);
            }
            if (!reproduciendo_id.empty()) reproduciendo_id.clear();
            grab_escuchando = false;
            visor = true;
            visor_video = true;
            visor_ruta = ruta;
            float x, y, w, h;
            rect_video(*this, x, y, w, h);
            float e = g.dpi / 96.0f;
            SetWindowPos(ventana_video, HWND_TOP, (int)(x * e), (int)(y * e), (int)(w * e), (int)(h * e), SWP_SHOWWINDOW);
            reproductor.abrir(ruta, ventana_video);
            reproductor.velocidad(velocidad_audio);
            reproductor.reproducir();
            pedir_dibujo();
        });
    });
}

void App::cerrar_visor() {
    if (visor_video) {
        reproductor.parar();
        if (ventana_video) ShowWindow(ventana_video, SW_HIDE);
        visor_video = false;
    }
    visor = false;
    pedir_dibujo();
}

// Clicks dentro del visor: controles del video, o cerrar.
bool App::click_visor(float x, float y) {
    if (!visor) return false;
    if (visor_video) {
        float vx, vy, vw, vh;
        rect_video(*this, vx, vy, vw, vh);
        float by = vy + vh + 30;  // la barra de progreso
        // Velocidad, a la derecha de todo.
        if (x > vx + vw - 50 && y > by - 14 && y < by + 14) {
            velocidad_audio = velocidad_audio == 1.0 ? 1.5 : (velocidad_audio == 1.5 ? 2.0 : 1.0);
            reproductor.velocidad(velocidad_audio);
            return true;
        }
        if (y > by - 12 && y < by + 12 && x > vx + 60 && x < vx + vw - 140) {
            double d = reproductor.duracion();
            if (d > 0) reproductor.ir_a(d * (x - vx - 60) / (vw - 200));
            visor_seek = true;
            seek_x = vx + 60;
            seek_w = vw - 200;
            return true;
        }
        if (x > vx && x < vx + 50 && y > by - 20 && y < by + 20) {
            if (reproductor.terminado()) {
                reproductor.ir_a(0);
                reproductor.reproducir();
            } else if (reproductor.pausado()) reproductor.reproducir();
            else reproductor.pausar();
            return true;
        }
        if (x > vx && x < vx + vw && y > vy && y < vy + vh) {
            if (reproductor.pausado()) reproductor.reproducir();
            else reproductor.pausar();
            return true;
        }
    }
    // Foto: apretar sobre ella empieza el paneo; afuera cierra.
    float ix, iy, iw, ih;
    if (rect_visor(ix, iy, iw, ih) && x >= ix && x <= ix + iw && y >= iy && y <= iy + ih) {
        visor_arrastrando = true;
        visor_ax = x;
        visor_ay = y;
        visor_mov = 0;
        return true;
    }
    cerrar_visor();
    return true;
}

void App::dibujar_visor() {
    if (!visor) return;
    g.rect(0, 0, g.ancho, g.alto, Color(0x000000, 0.92f));
    if (visor_video) {
        float vx, vy, vw, vh;
        rect_video(*this, vx, vy, vw, vh);
        float e = g.dpi / 96.0f;
        if (ventana_video)
            SetWindowPos(ventana_video, HWND_TOP, (int)(vx * e), (int)(vy * e), (int)(vw * e), (int)(vh * e), SWP_SHOWWINDOW | SWP_NOACTIVATE);
        float by = vy + vh + 30;
        bool pausado = reproductor.pausado() || reproductor.terminado();
        g.circulo(vx + 24, by, 18, Color(0xffffff, 0.15f));
        if (pausado) g.renglon(L"▶", vx + 16, by - 11, 16, Color(0xffffff));
        else {
            g.rect(vx + 18, by - 7, 4, 14, Color(0xffffff));
            g.rect(vx + 26, by - 7, 4, 14, Color(0xffffff));
        }
        double d = reproductor.duracion(), pos = reproductor.posicion();
        float f = d > 0 ? (float)std::clamp(pos / d, 0.0, 1.0) : 0;
        g.rect_redondo(vx + 60, by - 2, vw - 200, 4, 2, Color(0xffffff, 0.25f));
        g.rect_redondo(vx + 60, by - 2, (vw - 200) * f, 4, 2, Color(0x00a884));
        g.circulo(vx + 60 + (vw - 200) * f, by, 6, Color(0x00a884));
        auto mmss = [](double s) {
            int t = (int)s;
            return std::to_wstring(t / 60) + L":" + (t % 60 < 10 ? L"0" : L"") + std::to_wstring(t % 60);
        };
        g.renglon(mmss(pos) + L" / " + mmss(d), vx + vw - 130, by - 9, 12, Color(0xffffff));
        // Velocidad (la misma que los audios).
        g.rect_redondo(vx + vw - 46, by - 12, 44, 24, 12, Color(0xffffff, 0.2f));
        const wchar_t* vt = velocidad_audio == 1.5 ? L"1.5x" : (velocidad_audio == 2.0 ? L"2x" : L"1x");
        float vtw = g.medir(vt, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(vt, vx + vw - 46 + (44 - vtw) / 2, by - 8, 12, Color(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.renglon(L"✕", g.ancho - 40, 12, 22, Color(0xe9edef));
        necesita_dibujar = !pausado;
        return;
    }
    Imagen& im = imagen(visor_clave, visor_url, visor_animado);
    ID2D1Bitmap1* b = im.cuadro_actual(g, ahora);
    if (!im.anim.cuadros.empty()) necesita_dibujar = true;
    if (!b) {
        std::wstring t = im.fallo ? L"Not on WhatsApp's servers anymore. Asked your phone to re-upload it..." : L"Loading...";
        float tw = g.medir(t, 15);
        g.renglon(t, (g.ancho - tw) / 2, g.alto / 2 - 10, 15, Color(0x8696a0));
        return;
    }
    float x, y, w, h;
    rect_visor(x, y, w, h);
    g.bitmap(b, x, y, w, h);
    if (visor_zoom > 1.0f) {
        wchar_t z[16];
        swprintf(z, 16, L"%.0f%%", visor_zoom * 100);
        g.renglon(z, 16, 16, 14, Color(0xe9edef));
    }
    g.renglon(L"✕", g.ancho - 40, 16, 22, Color(0xe9edef));
}

// Escape: cierra lo que este abierto, en orden de "mas encima".
void App::escapar() {
    if (visor) {
        cerrar_visor();
    } else if (emojis_abierto) {
        emojis_abierto = false;
        emoji_buscador.foco = false;
        campo.foco = true;
    } else if (info_abierto) {
        cerrar_info();
    } else if (grab != Grab::Nada) {
        grabar_cancelar();
    } else if (reenviando) {
        reenviando = false;
    } else if (editando) {
        editando.reset();
        campo.poner(L"");
    } else if (respondiendo) {
        respondiendo.reset();
    } else if (adjunto) {
        adjunto.reset();
    } else if (buscador.foco) {
        buscador.poner(L"");
        ultima_busqueda.clear();
        resultados.clear();
        buscador.foco = false;
        campo.foco = true;
    } else if (sel_msg >= 0) {
        sel_msg = -1;
    }
    pedir_dibujo();
}

void App::doble_click(float x, float y) {
    if (x < ancho_lista || visor) return;
    float ym = 0;
    int i = mensaje_en(y, &ym);
    if (i < 0) return;
    // Doble click en una palabra la selecciona.
    size_t idx = 0;
    if (en_texto(i, ym, x, y, &idx)) {
        const std::wstring& t = mensajes[i].texto;
        size_t a = idx, b = idx;
        while (a > 0 && !iswspace(t[a - 1])) a--;
        while (b < t.size() && !iswspace(t[b])) b++;
        sel_msg = i;
        sel_a = a;
        sel_b = b;
        pedir_dibujo();
    }
}
