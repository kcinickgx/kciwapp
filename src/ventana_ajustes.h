// Ventana de ajustes: una ventana propia (dueno = la principal, no modal)
// con su Gfx, que edita en vivo el Ajustes global de tema.h. Todo cambio se
// aplica con ajustes::cambiar(...) asi que la ventana principal lo ve solo
// mirando ajustes::revision() (ella ya lo hace en App::aplicar_ajustes).
#pragma once
#include <windows.h>

namespace ventana_ajustes {

// Crea la ventana si no existe, o la trae al frente si ya esta abierta.
void abrir(HWND principal);
bool abierta();
void cerrar();

}  // namespace ventana_ajustes
