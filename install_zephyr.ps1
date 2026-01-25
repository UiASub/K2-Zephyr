# Zephyr RTOS Setup Script for Windows (PowerShell)
# Run as Administrator.
# Run with: powershell -ExecutionPolicy Bypass -File install_zephyr.ps1

$ErrorActionPreference = "Stop"
$ZephyrPath = "$HOME\zephyrproject"
$ZephyrVersion = "v4.2.0"

function Invoke-Step {
    param(
        [string]$Label,
        [ScriptBlock]$Command
    )
    Write-Host ">>> $Label" -ForegroundColor Cyan
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Label"
    }
}

# 1. Admin check
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Warning "This script requires Administrator privileges for package installation."
    Write-Warning "Please right-click PowerShell and select 'Run as Administrator'."
    exit 1
}

# 2. Install dependencies via winget
Invoke-Step "Enable winget (App Installer)" {
    Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe
}

$packages = @(
    "7zip.7zip",
    "Git.Git",
    "Kitware.CMake",
    "Ninja-build.Ninja",
    "oss-winget.dtc",
    "oss-winget.gperf",
    "Python.Python.3.11",
    "wget"
)

if (Get-Command winget -ErrorAction SilentlyContinue) {
    foreach ($pkg in $packages) {
        Write-Host "Installing/Updating $pkg..." -ForegroundColor Gray
        winget install $pkg --source winget --accept-package-agreements --accept-source-agreements --silent
        if ($LASTEXITCODE -ne 0) {
            throw "winget install failed for $pkg"
        }
    }
} else {
    Write-Error "Winget is not installed. Please install App Installer from the Microsoft Store."
    exit 1
}

Invoke-Step "Update PATH with 7-Zip" {
    [Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path", "Machine") + ";C:\Program Files\7-Zip", "Machine")
}

# 3. Refresh Environment Variables
foreach($level in "Machine","User") {
   [Environment]::GetEnvironmentVariables($level).GetEnumerator() | ForEach-Object {
       if($_.Name -ne "PSModulePath") { # Avoid breaking PS modules
           [Environment]::SetEnvironmentVariable($_.Name, $_.Value, "Process")
       }
   }
}

# 4. Create and activate virtual environment (Python 3.11)
if (-not (Test-Path "$ZephyrPath\.venv")) {
    Write-Host "Creating virtual environment at $ZephyrPath\.venv..." -ForegroundColor Green
    if (Get-Command py -ErrorAction SilentlyContinue) {
        Invoke-Step "Create venv (py -3.11 -m venv)" { py -3.11 -m venv "$ZephyrPath\.venv" }
    } else {
        Invoke-Step "Create venv (python -m venv)" { python -m venv "$ZephyrPath\.venv" }
    }
}

Write-Host "Activating virtual environment..." -ForegroundColor Green
. "$ZephyrPath\.venv\Scripts\Activate.ps1"

Invoke-Step "pip install west" { pip install west }

# 5. Initialize Zephyr workspace
if (-not (Test-Path "$ZephyrPath\.west")) {
    if (Test-Path $ZephyrPath) {
        if ((Get-ChildItem $ZephyrPath).Count -gt 0) {
            Write-Warning "Directory $ZephyrPath exists and is not empty. 'west init' might fail."
            Write-Warning "If the script fails, delete $ZephyrPath and try again."
        }
    }
    Invoke-Step "west init --manifest-rev $ZephyrVersion $ZephyrPath" { west init --manifest-rev $ZephyrVersion $ZephyrPath }
}

Set-Location $ZephyrPath

Invoke-Step "west update" { west update }
Invoke-Step "west zephyr-export" { west zephyr-export }

$pipPackages = (west packages pip) -replace "`r","" | Where-Object { $_ -ne "" }
if ($pipPackages.Count -gt 0) {
    Invoke-Step "pip install (west packages pip)" { pip install $pipPackages }
}

Set-Location "$ZephyrPath\zephyr"
Invoke-Step "west sdk install --toolchains arm-zephyr-eabi" { west sdk install --toolchains arm-zephyr-eabi }
Invoke-Step "west config --local build.board nucleo_f767zi" { west config --local build.board nucleo_f767zi }

# 6. Clone K2-Zephyr
if (-not (Test-Path "$ZephyrPath\\K2-Zephyr")) {
    Invoke-Step "Clone K2-Zephyr" { git clone https://github.com/UiASub/K2-Zephyr.git "$ZephyrPath\\K2-Zephyr" }
}

Write-Host "`nZephyr setup complete!" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
Write-Host "Remember to install STM32CubeProgrammer for faster flashing. OpenOCD works as an alternative." -ForegroundColor Yellow
Write-Host "=========================================" -ForegroundColor Green
Write-Host "Location: $ZephyrPath" -ForegroundColor Gray
Write-Host "IMPORTANT: Always activate the environment before working:" -ForegroundColor Yellow
Write-Host ". $ZephyrPath\.venv\Scripts\Activate.ps1" -ForegroundColor White
Write-Host "If you use VSCode make sure to read the README in K2-Zephyr/.vscode for proper setup." -ForegroundColor Yellow
