cmake_minimum_required(VERSION 3.20)
project(kciwapp2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kciwapp2 WIN32
    src/main.cpp
    src/gfx.cpp
    src/json.cpp
    src/red.cpp
    src/app.cpp
    src/campo.cpp
    src/acciones.cpp
    src/cache.cpp
    src/aviso.cpp
    src/emoji.cpp
    src/archivos.cpp
    src/reproductor.cpp
    src/grabador.cpp
    src/audio_ui.cpp
    src/tema.cpp
    src/emoji_ui.cpp
    src/info_ui.cpp
    src/ventana_ajustes.cpp
    src/reenviar_ui.cpp
    src/menu_ui.cpp
    src/buscar_chat.cpp
    src/webwa.cpp
    src/llamadas_ui.cpp
    src/ventana_llamada.cpp
    src/core.cpp
    src/config_ui.cpp
    src/cuentas.cpp
    src/actualizar.cpp
    src/actualizar_ui.cpp
    src/toast.cpp
    src/app.rc
)
target_include_directories(kciwapp2 PRIVATE third_party/webview2/include)
target_compile_definitions(kciwapp2 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kciwapp2 PRIVATE /W3 /utf-8 /EHsc /permissive-)
target_link_libraries(kciwapp2 PRIVATE d3d11 d2d1 dwrite dxgi windowscodecs winhttp dwmapi shell32 ole32 uxtheme comctl32 shlwapi comdlg32 mmdevapi avrt bcrypt)

# El instalador: un exe chico y autonomo (CRT estatica) que baja todo de
# kcinick.gxzone.com/kciwapp. publicar.py lo sube como kciwapp-setup.exe.
add_executable(kciwapp-setup WIN32 src/instalador/instalador.cpp src/app.rc)
target_compile_definitions(kciwapp-setup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kciwapp-setup PRIVATE /W3 /utf-8 /EHsc /permissive-)
set_property(TARGET kciwapp-setup PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(kciwapp-setup PRIVATE winhttp bcrypt ole32 shell32 gdi32 user32)
# Instala en Program Files: pide elevacion al abrirse.
target_link_options(kciwapp-setup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
