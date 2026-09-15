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
    src/app.rc
)
target_compile_definitions(kciwapp2 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(kciwapp2 PRIVATE /W3 /utf-8 /EHsc /permissive-)
target_link_libraries(kciwapp2 PRIVATE d3d11 d2d1 dwrite dxgi windowscodecs winhttp dwmapi shell32 ole32 uxtheme comctl32 shlwapi comdlg32)

# La salida va a portable\: exe + servidor.json + datos\.
set_target_properties(kciwapp2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/portable)
