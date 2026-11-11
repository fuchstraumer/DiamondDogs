# Cooks the noise shaders (assets/shaders/compute/Noise) for wgsl, with Lodestone's own cooker, so that Tint validates
# the WGSL. The DiamondDogs build cooks SPIR-V only: it builds Lodestone without Tint (cmake/ConfigureLodestone.cmake).
# This keeps the noise library portable to WebGPU.
#
# Build Lodestone first (Lodestone: scripts\build.bat RelWithDebInfo). Run from the DiamondDogs root:
#   pwsh assets/scripts/cook_noise_wgsl.ps1
# It exits with the cooker's exit code.
$Cooker = "$PSScriptRoot/../../../Lodestone/build/ninja-msvc/tools/cooker_console/RelWithDebInfo/lodestone_cooker_console.exe"
$Output = "$PSScriptRoot/../../build/noise-wgsl"

$noiseFolder = Resolve-Path "$PSScriptRoot/../shaders/compute/Noise"
$modules = @(
    "$noiseFolder/NoiseKernels.slang",
    "$noiseFolder/NoiseSampleGraph.slang"
)

if (-not (Test-Path $Cooker))
{
    Write-Error "No Lodestone cooker at $Cooker. Build Lodestone first."
    exit 1
}

New-Item -ItemType Directory -Force $Output | Out-Null
& $Cooker -o $Output --cache-dir "$Output/cache" --target=wgsl --quiet @modules
exit $LASTEXITCODE
