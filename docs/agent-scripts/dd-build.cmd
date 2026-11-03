@echo off
rem usage: dd-build.cmd <preset> <config> [configure]
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SoftwareDev\DiamondDogs
if "%3"=="configure" (
    cmake --preset %1 || exit /b 1
)
cmake --build build/%1 --config %2 --target foundation_tests rhi_system_tests platform_system_tests resource_context_tests TriangleTest ComputeManifestTest OceanFftTest OceanFft OceanFftBenchmark GpuPyramidTest ImGuiTest color_pipe_tests ColorMathTest ColorPipeTest -- -k 0
