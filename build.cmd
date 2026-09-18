@echo off
setlocal
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
rem version.h: VERSION (archivo), commit corto y fecha del build; lo muestra About.
set /p KVER=<VERSION
for /f %%c in ('git rev-parse --short HEAD 2^>nul') do set KCOMMIT=%%c
if "%KCOMMIT%"=="" set KCOMMIT=local
> build\version.h.tmp echo #define KCIWAPP_VERSION L"%KVER%"
>> build\version.h.tmp echo #define KCIWAPP_COMMIT L"%KCOMMIT%"
for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"') do set KFECHA=%%d
>> build\version.h.tmp echo #define KCIWAPP_FECHA L"%KFECHA%"
fc build\version.h.tmp build\version.h >nul 2>&1 || copy /y build\version.h.tmp build\version.h >nul
del build\version.h.tmp
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build || exit /b 1
rem El exe queda en build\; publicar.py lo sube a H:\kciwapp y el portable lo actualiza el usuario a mano.
echo build\kciwapp2.exe listo
