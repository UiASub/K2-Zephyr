Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$workspace = Split-Path -Parent $projectPath
$venvActivateWindows = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$venvActivateUnix = Join-Path -Path $workspace -ChildPath '.venv/bin/Activate.ps1'
$board = 'nucleo_h755zi_q/stm32h755xx/m7'
$boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
$otaBuild = $true

function Show-Usage {
    Write-Host 'Usage: .\build.ps1 [--h7|--H7] [--ota|--OTA] [--no-ota|--NO-OTA]'
    Write-Host 'Defaults to the H7 OTA build. Use --no-ota only for a plain non-MCUboot development build.'
}

function Show-FlashGuidance {
    Write-Host ''
    Write-Host 'K2 flashing:'
    Write-Host '  Ignore Zephyr''s generic "west flash" hint unless you are doing first-time USB'
    Write-Host '  provisioning or debug recovery.'
    Write-Host '  Normal updates should use: .\tools\k2-ota.ps1'
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
        }
        '--f7' {
            Write-Host "F7 support has been sunset. Use the H7 target: $board"
            exit 1
        }
        '--ota' {
            $otaBuild = $true
        }
        '--no-ota' {
            $otaBuild = $false
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
if ($otaBuild) {
    $buildDir = 'build-h755-ota'
    Write-Host "Building K2-Zephyr OTA image for $boardLabel..."
    Invoke-West build --sysbuild -p -b $board -d $buildDir $projectPath -- "-DEXTRA_CONF_FILE=ota.conf"
    Write-Host "OTA build complete for $boardLabel."
    Write-Host "Signed image: $buildDir\*\zephyr\zephyr.signed.bin"
    Show-FlashGuidance
} else {
    $buildDir = 'build-h755'
    Write-Host "Building K2-Zephyr non-OTA image for $boardLabel..."
    Invoke-West build -p -b $board -d $buildDir $projectPath
    Write-Host "Non-OTA build complete for $boardLabel."
    Write-Host "Use west flash -d $buildDir only when you intentionally need USB flashing."
}
