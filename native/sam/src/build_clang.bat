@echo off
rem Portability check: build libsam + sam_say with clang in strict C99 mode.
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "D:\VisualStudio\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d %~dp0
if not exist ..\build\clang mkdir ..\build\clang
set CLANG=D:\VisualStudio\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe
"%CLANG%" -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -O2 -D_CRT_SECURE_NO_WARNINGS sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c -o ..\build\clang\sam_say.exe
"%CLANG%" -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -O2 -D_CRT_SECURE_NO_WARNINGS sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_tts.c sam_cli.c -o ..\build\clang\sam.exe
