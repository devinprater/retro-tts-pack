@echo off
rem Build the OneCore acoustic backend port with MSVC. Usage: build.bat [x64^|x86^|all]   (output: build folder next to src)
rem Run it from PowerShell or a plain cmd window (vswhere/vcvarsall find the Windows SDK there).
if /i "%1"=="all" (call "%~f0" x64 || exit /b 1) & (call "%~f0" x86 || exit /b 1) & goto :eof
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSCMD_VER call :findvs
cd /d %~dp0
if not exist ..\build\%ARCH% mkdir ..\build\%ARCH%
rem /fp:precise: no contraction, IEEE single/double (x86 uses SSE2 by default)
set CF=/nologo /O2 /W4 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\
set SRC=zb_apm.c zb_dur.c zb_mlpg.c zb_vocoder.c zb_synth.c zb_synth_int.c zb_vocoder_int.c zb_io.c zb_sonic.c zb_ratechg.c zb_wave.c
cl %CF% zb_backtest.c %SRC% /Fe:..\build\%ARCH%\zb_backtest.exe || exit /b 1
rem the whole voice: text -> zf1 (text processing) -> zf2 (prosody, features) -> zb (acoustics) -> WAV
set FE1=zf1_ana.c zf1_dat.c zf1_engine.c zf1_frag.c zf1_fst.c zf1_fstpm.c zf1_lex.c zf1_lts.c zf1_modules.c zf1_morph.c zf1_poly.c zf1_pos.c zf1_post.c zf1_pron.c zf1_pron_oov.c zf1_ss.c zf1_tn.c zf1_util.c zf1_wb.c
set FE2=zf2_feat.c zf2_main.c zf2_prosody.c zf2_tree.c zf2_units.c
cl %CF% zira_say.c %FE1% %FE2% %SRC% /Fe:..\build\%ARCH%\zira_say.exe || exit /b 1
rem the host / screen-reader library (zira_tts.h): its CLI, the library tests and a DLL
set TTSSRC=zira_tts.c %FE1% %FE2% %SRC%
cl %CF% zira_cli.c %TTSSRC% /Fe:..\build\%ARCH%\zira.exe || exit /b 1
cl %CF% /I. ..\tests\lib\lib_test.c %TTSSRC% /Fe:..\build\%ARCH%\zira_lib_test.exe || exit /b 1
if not exist ..\build\%ARCH%\dll mkdir ..\build\%ARCH%\dll
cl /nologo /O2 /W4 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\dll\ /LD /DZIRA_BUILD_DLL %TTSSRC% /Fe:..\build\%ARCH%\zira_tts.dll || exit /b 1
rem ---- dist: the Windows library drop - the DLL with what another program needs to use it
set DIST=..\build\%ARCH%\dist
if not exist %DIST% mkdir %DIST%
copy /y ..\build\%ARCH%\zira_tts.dll %DIST% >nul
copy /y ..\build\%ARCH%\zira_tts.lib %DIST% >nul 2>nul
copy /y ..\build\%ARCH%\zira.exe %DIST% >nul
copy /y zira_tts.h %DIST% >nul
copy /y zb.h %DIST% >nul
> %DIST%\README.txt echo Microsoft David, Zira and Mark (Windows OneCore TTS) - portable C port (%ARCH%)
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo   zira_tts.dll, zira_tts.lib   the library: include zira_tts.h and link zira_tts.lib
>> %DIST%\README.txt echo   zira.exe                     the command line front end
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo No voice data is included. Point --dir at C:\Windows\Speech_OneCore\Engines\TTS\en-US
>> %DIST%\README.txt echo (M1033David.APM / .BEP / .INI and MSTTSLocEnUS.dat) from your own installation.
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo Other platforms: make voice (libzira_tts.so), see the Makefile.
echo dist: %DIST%
goto :eof

:findvs
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
goto :eof
