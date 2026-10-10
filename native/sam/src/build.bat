@echo off
rem Build libsam tools with MSVC. Usage: build.bat [x86|x64]
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCINSTALLDIR call :findvs
cd /d %~dp0
if not exist ..\build\%ARCH% mkdir ..\build\%ARCH%
set CF=/nologo /O2 /W4 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\
cl %CF% sam.c samsynth.c /Fe:..\build\%ARCH%\samsynth.exe || exit /b 1
cl %CF% sam_lex.c sam_morph.c lextest.c /Fe:..\build\%ARCH%\lextest.exe || exit /b 1
if exist sam_front.c cl %CF% sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c /Fe:..\build\%ARCH%\sam_say.exe || exit /b 1
set LIB_SRC=sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_tts.c
if exist sam_tts.c cl %CF% sam_cli.c %LIB_SRC% /Fe:..\build\%ARCH%\sam.exe || exit /b 1
if exist sam_tts.c cl %CF% /LD /DSAM_BUILD_DLL %LIB_SRC% /Fe:..\build\%ARCH%\sam.dll || exit /b 1
if exist ..\tests\lib\lib_test.c cl %CF% ..\tests\lib\lib_test.c %LIB_SRC% /Fe:..\build\%ARCH%\lib_test.exe || exit /b 1
rem standalone build with the voice data compiled in (run tools\embed_data.py first)
if exist sam_data.c cl %CF% /DSAM_EMBEDDED sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c sam_data.c /Fe:..\build\%ARCH%\sam_standalone.exe || exit /b 1
rem ---- dist: the Windows library drop - the DLL with what another program needs to use it
set DIST=..\build\%ARCH%\dist
if not exist %DIST% mkdir %DIST%
copy /y ..\build\%ARCH%\sam.dll %DIST% >nul
copy /y ..\build\%ARCH%\sam.lib %DIST% >nul 2>nul
copy /y ..\build\%ARCH%\sam.exe %DIST% >nul
copy /y sam_tts.h %DIST% >nul
copy /y sam.h %DIST% >nul
copy /y sam4fx.h %DIST% >nul
> %DIST%\README.txt echo Microsoft Sam, Mike and Mary - portable C port (%ARCH%)
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo   sam.dll, sam.lib   the library: include sam_tts.h and link sam.lib
>> %DIST%\README.txt echo   sam.exe            the command line front end
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo No voice data is included. Point --data at a folder holding Sam.spd,
>> %DIST%\README.txt echo LTTS1033.LXA and r1033tts.LXA from your own installation.
>> %DIST%\README.txt echo.
>> %DIST%\README.txt echo Other platforms: make lib (libsam.so / .dylib), see the Makefile.
echo dist: %DIST%
goto :eof

:findvs
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
goto :eof
