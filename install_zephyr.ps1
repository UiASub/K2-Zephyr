# Zephyr RTOS Setup Script for Windows (PowerShell)
# Run with: powershell -ExecutionPolicy Bypass -File install_zephyr.ps1

$ErrorActionPreference = "Stop"

$ZephyrPath = "$HOME\zephyrproject"

# Dependencies - Check if winget is installed
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Host "Winget is not installed. Please install it from Microsoft Store and rerun this script." -ForegroundColor Red
    exit 1
}

Write-Host "Installing dependencies via winget..." -ForegroundColor Green
winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja --silent --accept-package-agreements --accept-source-agreements
winget install --id GnuWin32.Gperf --silent --accept-package-agreements --accept-source-agreements
winget install --id Python.Python.3.11 --silent --accept-package-agreements --accept-source-agreements
winget install --id Git.Git --silent --accept-package-agreements --accept-source-agreements
winget install --id GnuWin32.Wget --silent --accept-package-agreements --accept-source-agreements
winget install --id 7zip.7zip --silent --accept-package-agreements --accept-source-agreements
winget install --id STMicroelectronics.STM32CubeProgrammer --silent --accept-package-agreements --accept-source-agreements

# Refresh environment variables so newly installed tools are visible in this session
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

# Prefer Python 3.11 (windows-curses wheel support) but fall back to the default python if needed
$PythonCommand = "python"
if (Get-Command py -ErrorAction SilentlyContinue) {
    py -3.11 --version *> $null
    if ($LASTEXITCODE -eq 0) {
        $PythonCommand = "py -3.11"
    }
}
Write-Host "Using $PythonCommand to manage the virtual environment (Python 3.11 recommended on Windows)." -ForegroundColor Cyan

# Create virtual environment if it doesn't exist
if (-not (Test-Path "$ZephyrPath\.venv")) {
    Write-Host "Creating virtual environment..." -ForegroundColor Green
    New-Item -ItemType Directory -Force -Path $ZephyrPath | Out-Null
    & $PythonCommand -m venv "$ZephyrPath\.venv"
}

# Activate virtual environment
Write-Host "Activating virtual environment..." -ForegroundColor Green
. "$ZephyrPath\.venv\Scripts\Activate.ps1"

# Use the venv's python for all pip operations to avoid PATH surprises
$VenvPython = Join-Path "$ZephyrPath\.venv\Scripts" "python.exe"

Write-Host "Upgrading pip in the virtual environment..." -ForegroundColor Green
& $VenvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to upgrade pip inside the virtual environment."
    exit 1
}

# Install west if not already installed
if (-not (Get-Command west -ErrorAction SilentlyContinue)) {
    Write-Host "Installing west 1.5.0..." -ForegroundColor Green
    & $VenvPython -m pip install west==1.5.0
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to install west."
        exit 1
    }
}

# Initialize workspace if not already initialized
if (-not (Test-Path "$ZephyrPath\.west")) {
    Write-Host "Initializing Zephyr workspace..." -ForegroundColor Green
    west init $ZephyrPath
}

Set-Location $ZephyrPath

# Update Zephyr and modules
Write-Host "Updating Zephyr and modules..." -ForegroundColor Green
west update

# Export Zephyr CMake package
Write-Host "Exporting Zephyr CMake package..." -ForegroundColor Green
west zephyr-export

# Install Python dependencies
if (Test-Path "$ZephyrPath\zephyr\scripts\requirements.txt") {
    Write-Host "Installing Python dependencies from Zephyr requirements..." -ForegroundColor Green
    & $VenvPython -m pip install -r "$ZephyrPath\zephyr\scripts\requirements.txt"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Warning: Python dependency installation had issues. Continuing anyway..." -ForegroundColor Yellow
    }
} else {
    Write-Host "No requirements.txt found, skipping Python dependencies installation." -ForegroundColor Yellow
}

# Install Zephyr SDK
Write-Host "Installing Zephyr SDK 0.17.4..." -ForegroundColor Green
Set-Location "$ZephyrPath\zephyr"
west sdk install --version 0.17.4

# Clone K2-Zephyr if not exists
Set-Location $ZephyrPath
if (-not (Test-Path "$ZephyrPath\K2-Zephyr")) {
    Write-Host "Cloning K2-Zephyr repository..." -ForegroundColor Green
    git clone https://github.com/UiASub/K2-Zephyr.git
}

Set-Location "$ZephyrPath\K2-Zephyr"

Write-Host "`nZephyr setup complete! Go to $ZephyrPath\K2-Zephyr to start working." -ForegroundColor Green
Write-Host "Remember to activate $ZephyrPath\.venv\Scripts\Activate.ps1 when you start a new terminal session." -ForegroundColor Yellow
