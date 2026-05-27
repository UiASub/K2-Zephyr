Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = $PSScriptRoot
if (-not $projectPath) {
    $projectPath = (Get-Location).Path
}

$workspace = Split-Path -Parent $projectPath
$venvActivate = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$board = 'nucleo_h755zi_q/stm32h755xx/m7'
$boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
$otaBuild = $false

function Invoke-West {
    & west @args
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

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

if (Test-Path $venvActivate) {
    & $venvActivate
} elseif (-not (Get-Command west -ErrorAction SilentlyContinue)) {
    Write-Host "west not found. Install west or create a Zephyr virtual environment at $workspace\.venv."
    exit 1
}

Set-Location $projectPath
if ($otaBuild) {
    $buildDir = if ($board -eq 'nucleo_f767zi') { 'build-f767-ota' } else { 'build-h755-ota' }
    Write-Host "Building K2-Zephyr OTA image for $boardLabel..."
    Invoke-West build --sysbuild -p -b $board -d $buildDir . -- "-DEXTRA_CONF_FILE=ota.conf"
    Write-Host "OTA build complete for $boardLabel! Flash with: west flash -d $buildDir"
} else {
    Write-Host "Building K2-Zephyr for $boardLabel..."
    Invoke-West build -p -b $board
    Write-Host "Build complete for $boardLabel! Flash with: west flash"
}
