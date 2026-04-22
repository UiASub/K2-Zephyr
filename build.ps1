Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspace = Join-Path -Path $HOME -ChildPath 'zephyrproject'
$venvActivate = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$projectPath = Join-Path -Path $workspace -ChildPath 'K2-Zephyr'
$board = 'nucleo_h755zi_q/stm32h755xx/m7'
$boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
$otaBuild = $false

foreach ($arg in $args) {
    switch ($arg.ToLowerInvariant()) {
        '--h7' {
            $board = 'nucleo_h755zi_q/stm32h755xx/m7'
            $boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
        }
        '--f7' {
            $board = 'nucleo_f767zi'
            $boardLabel = 'F7 (nucleo_f767zi)'
        }
        '--ota' {
            $otaBuild = $true
        }
        '--help' {
            Write-Host 'Usage: .\build.ps1 [--h7|--H7|--f7|--F7] [--ota|--OTA]'
            Write-Host 'Defaults to H7 if no board flag is provided. Use --ota for MCUboot + Ethernet OTA builds.'
            exit 0
        }
        default {
            Write-Host "Unknown option: $arg"
            Write-Host 'Usage: .\build.ps1 [--h7|--H7|--f7|--F7] [--ota|--OTA]'
            exit 1
        }
    }
}

if (-not (Test-Path $venvActivate)) {
    Write-Host "Virtualenv activation script not found: $venvActivate"
    exit 1
}

& $venvActivate

Set-Location $projectPath
if ($otaBuild) {
    $buildDir = if ($board -eq 'nucleo_f767zi') { 'build-f767-ota' } else { 'build-h755-ota' }
    Write-Host "Building K2-Zephyr OTA image for $boardLabel..."
    west build --sysbuild -p -b $board -d $buildDir . -- "-DEXTRA_CONF_FILE=ota.conf"
    Write-Host "OTA build complete for $boardLabel! Flash with: west flash -d $buildDir"
} else {
    Write-Host "Building K2-Zephyr for $boardLabel..."
    west build -p -b $board
    Write-Host "Build complete for $boardLabel! Flash with: west flash"
}
