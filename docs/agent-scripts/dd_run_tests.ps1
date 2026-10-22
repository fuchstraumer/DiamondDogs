param([string]$Preset = "ninja-msvc", [string]$Config = "Debug", [switch]$SyncEnv)
# Runs every DiamondDogs test that currently builds, and prints one line per test plus validation counts.
$build = "C:\SoftwareDev\DiamondDogs\build\$Preset\tests"
$logs = "$PSScriptRoot\logs\$Preset-$Config"
if ($SyncEnv) { $env:VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true" } else { Remove-Item Env:\VK_KHRONOS_VALIDATION_VALIDATE_SYNC -ErrorAction SilentlyContinue }
# OBS's capture hook layer reports its own validation messages (an NT-handle import, present hazards)
$env:VK_LOADER_LAYERS_DISABLE = 'VK_LAYER_OBS_HOOK'
New-Item -ItemType Directory -Force $logs | Out-Null
$tests = @(
    @{ Name = "foundation_tests"; Path = "$build\unit_tests\foundation\$Config\foundation_tests.exe"; Args = @("--gtest_brief=1") },
    @{ Name = "rhi_system_tests"; Path = "$build\unit_tests\rhi_system\$Config\rhi_system_tests.exe"; Args = @("--gtest_brief=1") },
    @{ Name = "resource_context_tests"; Path = "$build\unit_tests\resource_context\$Config\resource_context_tests.exe"; Args = @("--gtest_brief=1") },
    @{ Name = "platform_system_tests"; Path = "$build\unit_tests\platform_system\$Config\platform_system_tests.exe"; Args = @("--gtest_brief=1") },
    @{ Name = "ComputeManifestTest"; Path = "$build\integration_tests\ComputeManifestTest\$Config\ComputeManifestTest.exe"; Args = @() },
    @{ Name = "OceanFftTest"; Path = "$build\integration_tests\OceanFftTest\$Config\OceanFftTest.exe"; Args = @() },
    @{ Name = "GpuPyramidTest"; Path = "$build\integration_tests\GpuPyramidTest\$Config\GpuPyramidTest.exe"; Args = @() },
    @{ Name = "ImGuiTest"; Path = "$build\integration_tests\ImGuiTest\$Config\ImGuiTest.exe"; Args = @("--frames=180") },
    @{ Name = "TriangleTest"; Path = "$build\integration_tests\TriangleTest\$Config\TriangleTest.exe"; Args = @("--frames=240") },
    @{ Name = "ImGuiTest_resize"; Path = "$build\integration_tests\ImGuiTest\$Config\ImGuiTest.exe"; Args = @("--frames=150", "--resize") },
    @{ Name = "OceanFft"; Path = "C:\SoftwareDev\DiamondDogs\build\$Preset\applications\OceanFft\$Config\OceanFft.exe"; Args = @("--frames=240") },
    @{ Name = "TriangleTest_resize"; Path = "$build\integration_tests\TriangleTest\$Config\TriangleTest.exe"; Args = @("--frames=150", "--resize") }
)
$failed = 0
foreach ($test in $tests) {
    if (-not (Test-Path $test.Path)) { "{0,-24} NOT BUILT" -f $test.Name; $failed++; continue }
    $log = "$logs\$($test.Name).log"
    Push-Location (Split-Path $test.Path)
    $proc = Start-Process -FilePath $test.Path -ArgumentList $test.Args -NoNewWindow -PassThru -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
    if (-not $proc.WaitForExit(600000)) { $proc.Kill(); $code = "TIMEOUT" } else { $code = $proc.ExitCode }
    Pop-Location
    $text = (Get-Content "$log.out" -Raw) + (Get-Content "$log.err" -Raw)
    $vuids = ([regex]::Matches($text, "VUID_NAME:\S+|Validation Error")).Count
    $summary = ([regex]::Matches($text, "\[  (PASSED|FAILED) +\] [^\r\n]*") | ForEach-Object { $_.Value }) -join "; "
    "{0,-24} exit={1,-6} validation_messages={2,-3} {3}" -f $test.Name, $code, $vuids, $summary
    if ($code -ne 0 -or $vuids -ne 0) { $failed++ }
}
"failed: $failed"
exit $failed
