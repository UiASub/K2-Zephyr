# build.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "Setting up Zephyr environment..."
Set-Location "$HOME\zephyrproject"

# Activate venv (PowerShell)
$venvRoot = Join-Path -Path $HOME -ChildPath 'zephyrproject\.venv'
$activateCandidates = @(
    Join-Path -Path $venvRoot -ChildPath 'Scripts\Activate.ps1' # Windows
)

$activateScript = $activateCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $activateScript) {
    Write-Host "Virtualenv activation script not found under $venvRoot."
    exit 1
}

& $activateScript

Write-Host "Building K2-Zephyr project..."
Set-Location "$HOME\zephyrproject\K2-Zephyr"
west build -p -b nucleo_f767zi

Write-Host "Build complete! Flash with: west flash"