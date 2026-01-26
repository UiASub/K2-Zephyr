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
try {
    Write-Host ">>> Enable winget (App Installer)" -ForegroundColor Cyan
    Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe
} catch {
    Write-Warning "Failed to register App Installer (winget should already be installed): $_"
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
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
            Write-Warning "Failed to install $pkg (exit code: $LASTEXITCODE). May already be installed or failed."
        }
    }
} else {
    Write-Error "Winget is not installed. Please install App Installer from the Microsoft Store."
    exit 1
}

# 3. Refresh Environment Variables (pick up newly installed tools)
Write-Host "Refreshing environment variables..." -ForegroundColor Cyan
foreach($level in "Machine","User") {
   [Environment]::GetEnvironmentVariables($level).GetEnumerator() | ForEach-Object {
       if($_.Name -ne "PSModulePath") { # Avoid breaking PS modules
           [Environment]::SetEnvironmentVariable($_.Name, $_.Value, "Process")
       }
   }
}

# Also explicitly add common tool paths to current session
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

Write-Host "Verifying Git installation..." -ForegroundColor Cyan
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git is not found in PATH after installation. Please restart PowerShell and run the script again."
    exit 1
}
Write-Host "Git found: $(git --version)" -ForegroundColor Green

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

Write-Host "Installing Python dependencies..." -ForegroundColor Cyan
pip install -r "$ZephyrPath\zephyr\scripts\requirements.txt"

Set-Location "$ZephyrPath\zephyr"
Invoke-Step "west sdk install --toolchains arm-zephyr-eabi" { west sdk install --toolchains arm-zephyr-eabi }
Invoke-Step "west config --local build.board nucleo_f767zi" { west config --local build.board nucleo_f767zi }

# 6. Clone K2-Zephyr
if (-not (Test-Path "$ZephyrPath\\K2-Zephyr")) {
    Invoke-Step "Clone K2-Zephyr" { git clone https://github.com/UiASub/K2-Zephyr.git "$ZephyrPath\\K2-Zephyr" }
}

Write-Host "`nZephyr setup complete!" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Red
Write-Host "Remember to install STM32CubeProgrammer (see docs)." -ForegroundColor Yellow
Write-Host "=========================================" -ForegroundColor Red
Write-Host "Location: $ZephyrPath" -ForegroundColor Gray
Write-Host "IMPORTANT: Always activate the environment before working:" -ForegroundColor Yellow
Write-Host ". $ZephyrPath\.venv\Scripts\Activate.ps1" -ForegroundColor White
Write-Host "If you use VSCode make sure to read the README in K2-Zephyr/.vscode for proper setup." -ForegroundColor Yellow
