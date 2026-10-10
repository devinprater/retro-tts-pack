@echo off
rem cl64.bat <cl args...> : run MSVC x64 cl.exe with the VS environment set up (cwd = src).
rem Example: cl64.bat /nologo /O2 /W3 /Fo..\build\lts\ /Fe:..\build\zf1_t_lts.exe zf1_t_lts.c zf1_lts.c zf1_dat.c zf1_util.c
rem From git-bash: cmd //c "D:\llm-experiments\misk\zira\src\cl64.bat /nologo ..."
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;C:\Windows\System32;%PATH%"
if not defined VCINSTALLDIR call "D:\VisualStudio\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d %~dp0
cl /D_CRT_SECURE_NO_WARNINGS %*
