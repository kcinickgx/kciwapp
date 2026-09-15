#include "emoji.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <functional>

#include "archivos.h"
#include "red.h"  // ancho() / angosto(): UTF-8 <-> UTF-16

namespace emoji {
namespace {

std::vector<Emoji> g_emojis;
std::vector<std::wstring> g_categorias = {
    L"Smileys", L"Animals", L"Food", L"Activities", L"Travel", L"Objects", L"Symbols", L"Flags",
};
std::vector<std::wstring> g_recientes;
std::wstring g_ruta_recientes;

// Parte `datos` en lineas (separadas por \n, tolerando \r\n) y llama a f
// con cada una (sin el salto).
void por_linea(const std::string& datos, const std::function<void(const std::string&)>& f) {
    size_t inicio = 0;
    while (inicio <= datos.size()) {
        size_t fin = datos.find('\n', inicio);
        size_t largo = (fin == std::string::npos ? datos.size() : fin) - inicio;
        std::string linea = datos.substr(inicio, largo);
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (!linea.empty()) f(linea);
        if (fin == std::string::npos) break;
        inicio = fin + 1;
    }
}

std::wstring a_minusculas(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

}  // namespace

bool cargar(const std::wstring& carpeta_exe) {
    g_emojis.clear();
    std::string datos = archivos::leer_todo(carpeta_exe + L"\\emoji.txt");
    por_linea(datos, [](const std::string& linea) {
        size_t t1 = linea.find('\t');
        if (t1 == std::string::npos) return;
        size_t t2 = linea.find('\t', t1 + 1);
        if (t2 == std::string::npos) return;
        Emoji e;
        e.simbolo = ancho(linea.substr(0, t1));
        e.categoria = std::atoi(linea.substr(t1 + 1, t2 - t1 - 1).c_str());
        e.nombre = ancho(linea.substr(t2 + 1));
        g_emojis.push_back(std::move(e));
    });

    std::wstring carpeta_datos = carpeta_exe + L"\\datos";
    CreateDirectoryW(carpeta_datos.c_str(), nullptr);
    g_ruta_recientes = carpeta_datos + L"\\emoji-recientes.txt";
    g_recientes.clear();
    por_linea(archivos::leer_todo(g_ruta_recientes),
              [](const std::string& linea) { g_recientes.push_back(ancho(linea)); });

    return !g_emojis.empty();
}

const std::vector<Emoji>& todos() { return g_emojis; }

std::vector<const Emoji*> buscar(const std::wstring& texto) {
    std::wstring buscado = a_minusculas(texto);
    std::vector<const Emoji*> r;
    for (const auto& e : g_emojis) {
        if (a_minusculas(e.nombre).find(buscado) != std::wstring::npos) r.push_back(&e);
    }
    return r;
}

const std::vector<std::wstring>& categorias() { return g_categorias; }

std::vector<std::wstring> recientes() { return g_recientes; }

void usar(const std::wstring& simbolo) {
    g_recientes.erase(std::remove(g_recientes.begin(), g_recientes.end(), simbolo), g_recientes.end());
    g_recientes.insert(g_recientes.begin(), simbolo);
    if (g_recientes.size() > 30) g_recientes.resize(30);
    if (g_ruta_recientes.empty()) return;
    std::string datos;
    for (const auto& s : g_recientes) datos += angosto(s) + "\n";
    archivos::escribir_todo(g_ruta_recientes, datos);
}

}  // namespace emoji
