Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspace = Join-Path -Path $HOME -ChildPath 'zephyrproject'
$venvActivate = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$projectPath = Join-Path -Path $workspace -ChildPath 'K2-Zephyr'

if (-not (Test-Path $venvActivate)) {
    Write-Host "Virtualenv activation script not found: $venvActivate"
    exit 1
}

& $venvActivate

Set-Location $projectPath
west build -p -b nucleo_f767zi

Write-Host "Build complete! Flash with: west flash"