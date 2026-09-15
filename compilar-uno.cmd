@echo off
rem Compila un solo .cpp (chequeo de sintaxis), sin linkear: compilar-uno.cmd srcrchivo.cpp
setlocal
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio8\Community\VC\Auxiliary\Buildcvars64.bat" >nul
cd /d "%~dp0"
if not exist build\solo mkdir build\solo
cl /nologo /c /std:c++20 /utf-8 /EHsc /permissive- /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fobuild\solo\ %1
