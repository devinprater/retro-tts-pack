@echo off
rem Build a zf1 program (64-bit MSVC, C11 mode used only as a portable-C99 check).
rem   build_zf1.bat <main.c> [out.exe]
rem Library = every zf1_*.c in this folder except test/driver mains (zf1_t_*.c, zf1_main*.c).
setlocal enabledelayedexpansion
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if not defined VCINSTALLDIR call :findvs
cd /d %~dp0
if not exist ..\build mkdir ..\build
set LIB_SRC=
for %%f in (zf1_*.c) do (
  set "n=%%~nf"
  if /i not "!n:~0,6!"=="zf1_t_" if /i not "!n:~0,8!"=="zf1_main" set "LIB_SRC=!LIB_SRC! %%f"
)
set "OUT=%~2"
if "%OUT%"=="" set "OUT=..\build\%~n1.exe"
cl /nologo /O2 /W3 /std:c11 /D_CRT_SECURE_NO_WARNINGS /Fo..\build\ /Fe:%OUT% %1 %LIB_SRC% || exit /b 1
goto :eof

:findvs
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
goto :eof
