// Emoji: la tabla de emojis (cargada de emoji.txt, al lado del exe) y los
// recientes, persistidos en datos\emoji-recientes.txt.
#pragma once
#include <string>
#include <vector>

namespace emoji {

struct Emoji {
    std::wstring simbolo;
    int categoria = 0;
    std::wstring nombre;  // en ingles, como viene en emoji.txt
};

// Carga <carpeta_exe>\emoji.txt (formato: emoji TAB categoria TAB nombre,
// UTF-8) y los recientes de <carpeta_exe>\datos\emoji-recientes.txt.
// Devuelve si pudo cargar al menos un emoji.
bool cargar(const std::wstring& carpeta_exe);

const std::vector<Emoji>& todos();
// Por nombre, sin distinguir mayusculas, substring.
std::vector<const Emoji*> buscar(const std::wstring& texto);
// Nombres de categoria en ingles, indexados por Emoji::categoria.
const std::vector<std::wstring>& categorias();

// Los ultimos usados (el mas reciente primero), hasta 30.
std::vector<std::wstring> recientes();
// Anota un uso: lo manda al principio de recientes() y lo persiste.
void usar(const std::wstring& simbolo);

}  // namespace emoji
