@echo off
setlocal
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build || exit /b 1
rem El exe va a portable\; si esta corriendo, queda el de build\ para la proxima.
copy /y build\kciwapp2.exe portable\kciwapp2.exe >nul 2>&1 && echo copiado a portable\ || echo AVISO: portable\kciwapp2.exe esta en uso, no se actualizo
