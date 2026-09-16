@echo off
setlocal
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build || exit /b 1
rem El exe queda en build\; publicar.py lo sube a H:\kciwapp y el portable lo actualiza el usuario a mano.
echo build\kciwapp2.exe listo
