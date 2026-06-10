Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$workspace = Split-Path -Parent $projectPath
$venvActivateWindows = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$venvActivateUnix = Join-Path -Path $workspace -ChildPath '.venv/bin/Activate.ps1'
$board = 'nucleo_f767zi'
$boardLabel = 'F7 (nucleo_f767zi)'
$buildDir = 'build-f767-esc-debug'

function Show-Usage {
    Write-Host 'Usage: .\build.ps1 [--f7|--F7|--h7|--H7|--esp32-supermini]'
    Write-Host 'Builds the minimal ESC UART debug firmware. Defaults to F7.'
}

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
            $buildDir = 'build-h755-esc-debug'
        }
        '--f7' {
            $board = 'nucleo_f767zi'
            $boardLabel = 'F7 (nucleo_f767zi)'
            $buildDir = 'build-f767-esc-debug'
        }
        { $_ -in '--esp32-supermini', '--esp32c3-supermini', '--esp32' } {
            $board = 'esp32c3_supermini'
            $boardLabel = 'ESP32-C3 SuperMini (esp32c3_supermini)'
            $buildDir = 'build-esp32c3-supermini-esc-debug'
        }
        '--help' {
            Show-Usage
            exit 0
        }
        default {
            Write-Host "Unknown option: $arg"
            Show-Usage
            exit 1
        }
    }
}

if (Test-Path $venvActivateWindows) {
    & $venvActivateWindows
} elseif (Test-Path $venvActivateUnix) {
    & $venvActivateUnix
} elseif (-not (Get-Command west -ErrorAction SilentlyContinue)) {
    Write-Host "west not found. Install west or create a Zephyr virtual environment at $workspace\.venv."
    exit 1
}

Set-Location $projectPath
Write-Host "Building minimal ESC debug firmware for $boardLabel..."
Invoke-West build -p -b $board -d $buildDir $projectPath
Write-Host "ESC debug build complete for $boardLabel."
Write-Host "Flash with: west flash -d $buildDir"
