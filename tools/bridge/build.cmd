@echo off
rem Builds the 120 fps bridge: ts2bridge.asi (32-bit, into game\scripts) and ts2present64.exe (64-bit, into game\).
rem Absolute paths throughout: the vcvars scripts may change the working directory.
setlocal
set SRC=%~dp0
set ROOT=%~dp0..\..
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cl /nologo /LD /O2 /MT /W3 /EHsc /I "%ROOT%\toy2-decomp\external\include" "%SRC%ts2bridge.cpp" /Fe:"%ROOT%\game\scripts\ts2bridge.asi" /Fo:"%TEMP%\ts2bridge.obj" ^
   /link /NODEFAULTLIB:MSVCRTD "%ROOT%\toy2-decomp\external\libs\minhook_x86.lib" d3d11.lib dxgi.lib user32.lib /IMPLIB:"%TEMP%\ts2bridge.lib" || exit /b 1
endlocal
setlocal
set SRC=%~dp0
set ROOT=%~dp0..\..
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /O2 /MT /W3 /EHsc "%SRC%ts2present64.cpp" /Fe:"%ROOT%\game\ts2present64.exe" /Fo:"%TEMP%\ts2present64.obj" ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup d3d11.lib dxgi.lib d3dcompiler.lib user32.lib || exit /b 1
