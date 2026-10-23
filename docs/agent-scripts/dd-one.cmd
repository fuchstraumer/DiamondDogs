@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SoftwareDev\DiamondDogs
if "%3"=="configure" (cmake --preset %1 || exit /b 1)
setlocal EnableDelayedExpansion
set "TARGETS="
set "INDEX=0"
for %%t in (%*) do (
    set /a "INDEX+=1"
    if !INDEX! GEQ 4 set "TARGETS=!TARGETS! %%~t"
)
cmake --build build/%1 --config %2 --target !TARGETS! -- -k 0
