Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectPath = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$workspace = Split-Path -Parent $projectPath
$venvActivateWindows = Join-Path -Path $workspace -ChildPath '.venv\Scripts\Activate.ps1'
$venvActivateUnix = Join-Path -Path $workspace -ChildPath '.venv/bin/Activate.ps1'
$board = 'nucleo_h755zi_q/stm32h755xx/m7'
$boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
$otaBuild = $true
$otaArgSeen = $false

function Show-Usage {
    Write-Host 'Usage: .\build.ps1 [--h7|--H7|--f7|--F7] [--ota|--OTA] [--no-ota|--NO-OTA]'
    Write-Host 'Defaults to the H7 OTA build. F7 defaults to a plain development build.'
    Write-Host 'Use --no-ota only when you intentionally need a plain non-MCUboot development build.'
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
            $board = 'nucleo_h755zi_q/stm32h755xx/m7'
            $boardLabel = 'H7 (nucleo_h755zi_q/stm32h755xx/m7)'
        }
        '--f7' {
            $board = 'nucleo_f767zi'
            $boardLabel = 'F7 (nucleo_f767zi)'
            if (-not $otaArgSeen) {
                $otaBuild = $false
            }
        }
        '--ota' {
            $otaBuild = $true
            $otaArgSeen = $true
        }
        '--no-ota' {
            $otaBuild = $false
            $otaArgSeen = $true
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
    $buildDir = if ($board -eq 'nucleo_f767zi') { 'build-f767-ota' } else { 'build-h755-ota' }
    Write-Host "Building K2-Zephyr OTA image for $boardLabel..."
    Invoke-West build --sysbuild -p -b $board -d $buildDir $projectPath -- "-DEXTRA_CONF_FILE=ota.conf"
    Write-Host "OTA build complete for $boardLabel."
    Write-Host "Signed image: $buildDir\*\zephyr\zephyr.signed.bin"
    Show-FlashGuidance
} else {
    $buildDir = if ($board -eq 'nucleo_f767zi') { 'build-f767' } else { 'build-h755' }
    Write-Host "Building K2-Zephyr non-OTA image for $boardLabel..."
    Invoke-West build -p -b $board -d $buildDir $projectPath
    Write-Host "Non-OTA build complete for $boardLabel."
    Write-Host "Use west flash -d $buildDir only when you intentionally need USB flashing."
}
