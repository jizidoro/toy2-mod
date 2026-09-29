@echo off
rem Builds tools\ts2fix60\ts2fix60.cpp into game\scripts\ts2fix60.asi (32-bit, no dependencies).
setlocal
set SRC=%~dp0
set ROOT=%~dp0..\..
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cl /nologo /LD /O2 /MT /W3 "%SRC%ts2fix60.cpp" /Fe:"%ROOT%\game\scripts\ts2fix60.asi" /Fo:"%TEMP%\ts2fix60.obj" ^
   /link /IMPLIB:"%TEMP%\ts2fix60.lib" || exit /b 1
