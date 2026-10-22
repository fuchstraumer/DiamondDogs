@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SoftwareDev\DiamondDogs
if "%3"=="configure" (cmake --preset %1 || exit /b 1)
cmake --build build/%1 --config %2 --target %4 -- -k 0
