@echo off
rem Builds tools\ts2mods\ts2mods.cpp into game\scripts\ts2mods.asi (32-bit, VS2022 Build Tools, MinHook from toy2-decomp).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set ROOT=%~dp0..\..
cd /d "%~dp0"
cl /nologo /LD /O2 /MT /W3 /EHsc /std:c++17 /I "%ROOT%\toy2-decomp\external\include" ts2mods.cpp /Fe:"%ROOT%\game\scripts\ts2mods.asi" /Fo:"%TEMP%\ts2mods.obj" ^
   /link /NODEFAULTLIB:MSVCRTD "%ROOT%\toy2-decomp\external\libs\minhook_x86.lib" /IMPLIB:"%TEMP%\ts2mods.lib" || exit /b 1
