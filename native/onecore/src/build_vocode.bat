@echo off
rem Build the Anna-through-OneCore-vocoder experiment (zb_vocode_test.exe). Usage: build_vocode.bat
setlocal
set ARCH=x64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSCMD_VER call :findvs
cd /d %~dp0
if not exist ..\build\%ARCH%\vc mkdir ..\build\%ARCH%\vc
set CF=/nologo /O2 /W3 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\vc\
rem everything but zb_vocoder.c: that one is #included by the test
cl %CF% zb_vocode_test.c zb_apm.c zb_io.c zb_synth.c zb_synth_int.c zb_vocoder_int.c zb_dur.c zb_mlpg.c zb_wave.c zb_sonic.c zb_ratechg.c /Fe:..\build\%ARCH%\zb_vocode_test.exe || exit /b 1
goto :eof

:findvs
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
goto :eof
