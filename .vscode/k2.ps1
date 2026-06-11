param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("check", "build-no-ota", "build-ota", "flash-no-ota", "flash-ota", "build-uart-loopback", "flash-uart-loopback", "build-vesc-uart-duty", "flash-vesc-uart-duty", "build-vesc-all-duty", "flash-vesc-all-duty")]
    [string]$Action
)

$ErrorActionPreference = "Stop"

$VscodeDir = $PSScriptRoot
$RepoDir = Split-Path -Parent $VscodeDir
$ZephyrProjectDir = Split-Path -Parent $RepoDir

Write-Host "Repo dir: $RepoDir"
Write-Host "Zephyr project dir: $ZephyrProjectDir"
Write-Host ""

$SevenZipPath = "C:\Program Files\7-Zip"
if (Test-Path $SevenZipPath) {
    $env:Path = $env:Path + ";$SevenZipPath"
}

$ActivateScript = Join-Path $ZephyrProjectDir ".venv\Scripts\Activate.ps1"

if (!(Test-Path $ActivateScript)) {
    throw "Fant ikke Zephyr venv: $ActivateScript"
}

& $ActivateScript

Set-Location $RepoDir

if (!(Test-Path ".\CMakeLists.txt")) {
    throw "Fant ikke CMakeLists.txt i $RepoDir. Sjekk at VS Code har åpnet K2-Zephyr-mappen."
}

if (!(Test-Path ".\build.ps1")) {
    throw "Fant ikke build.ps1 i $RepoDir. Git-repoet er trolig ikke riktig klonet."
}

switch ($Action) {
    "check" {
        west --version
        west sdk list
    }

    "build-no-ota" {
        & ".\build.ps1" --H7 --NO-OTA
    }

    "build-ota" {
        & ".\build.ps1" --H7 --OTA
    }

    "flash-no-ota" {
        west flash -d build-h755
    }

    "flash-ota" {
        west flash -d build-h755-ota
    }

    "build-uart-loopback" {
        west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-uart-loopback tests/uart_loopback
    }

    "flash-uart-loopback" {
        west flash -d build-uart-loopback
    }

    "build-vesc-uart-duty" {
        west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-vesc-uart-duty tests/vesc_uart_duty
    }

    "flash-vesc-uart-duty" {
        west flash -d build-vesc-uart-duty
    }

    "build-vesc-all-duty" {
        west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-vesc-all-duty tests/vesc_all_duty
    }

    "flash-vesc-all-duty" {
        west flash -d build-vesc-all-duty
    }
}
