@echo off
rem Builds tools\ts2borderless\ts2borderless.cpp into game\scripts\ts2borderless.asi (32-bit, MinHook from toy2-decomp).
setlocal
set SRC=%~dp0
set ROOT=%~dp0..\..
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cl /nologo /LD /O2 /MT /W3 /I "%ROOT%\toy2-decomp\external\include" "%SRC%ts2borderless.cpp" /Fe:"%ROOT%\game\scripts\ts2borderless.asi" /Fo:"%TEMP%\ts2borderless.obj" ^
   /link /NODEFAULTLIB:MSVCRTD "%ROOT%\toy2-decomp\external\libs\minhook_x86.lib" user32.lib /IMPLIB:"%TEMP%\ts2borderless.lib" || exit /b 1
