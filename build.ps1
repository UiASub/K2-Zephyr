Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspace = Join-Path -Path $HOME -ChildPath 'zephyrproject'
$venvActivate = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$projectPath = Join-Path -Path $workspace -ChildPath 'K2-Zephyr'
$board = 'nucleo_h755zi_q/stm32h755xx/m7'
$boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'

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
        '--help' {
            Write-Host 'Usage: .\build.ps1 [--h7|--H7|--f7|--F7]'
            Write-Host 'Defaults to H7 if no board flag is provided.'
            exit 0
        }
        default {
            Write-Host "Unknown option: $arg"
            Write-Host 'Usage: .\build.ps1 [--h7|--H7|--f7|--F7]'
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
Write-Host "Building K2-Zephyr for $boardLabel..."
west build -p -b $board

Write-Host "Build complete for $boardLabel! Flash with: west flash"
