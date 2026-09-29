@echo off
rem Builds tools\ts2tex\ts2tex.cpp into game\scripts\ts2tex.asi (32-bit, MinHook from toy2-decomp, stb from vendor\stb).
setlocal
set SRC=%~dp0
set ROOT=%~dp0..\..
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cl /nologo /LD /O2 /MT /W3 /EHsc /std:c++17 /I "%ROOT%\toy2-decomp\external\include" /I "%ROOT%\vendor\stb" "%SRC%ts2tex.cpp" /Fe:"%ROOT%\game\scripts\ts2tex.asi" /Fo:"%TEMP%\ts2tex.obj" ^
   /link /NODEFAULTLIB:MSVCRTD "%ROOT%\toy2-decomp\external\libs\minhook_x86.lib" bcrypt.lib gdi32.lib user32.lib /IMPLIB:"%TEMP%\ts2tex.lib" || exit /b 1
