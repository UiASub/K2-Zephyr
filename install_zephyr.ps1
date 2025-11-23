# Zephyr RTOS Setup Script for Windows (PowerShell)
# Run as Administrator
# Run with: powershell -ExecutionPolicy Bypass -File install_zephyr.ps1

$ErrorActionPreference = "Stop"
$ZephyrPath = "$HOME\zephyrproject"
$ZephyrVersion = "v4.2.0"
$SdkVersion = "0.17.4"
$WestVersion = "1.5.0"

# 1. Check for Administrator privileges
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Warning "This script requires Administrator privileges for package installation."
    Write-Warning "Please right-click PowerShell and select 'Run as Administrator'."
    exit 1
}

# 2. Install Dependencies via Winget
Write-Host "Checking and installing dependencies..." -ForegroundColor Cyan

$dependencies = @(
    @{Id="Kitware.CMake"; Name="CMake"},
    @{Id="Ninja-build.Ninja"; Name="Ninja"},
    @{Id="GnuWin32.Gperf"; Name="Gperf"},
    @{Id="Python.Python.3.11"; Name="Python 3.11"},
    @{Id="Git.Git"; Name="Git"},
    @{Id="GnuWin32.Wget"; Name="Wget"},
    @{Id="7zip.7zip"; Name="7-Zip"},
    @{Id="STMicroelectronics.STM32CubeProgrammer"; Name="STM32CubeProgrammer"}
)

if (Get-Command winget -ErrorAction SilentlyContinue) {
    foreach ($dep in $dependencies) {
        Write-Host "Installing/Updating $($dep.Name)..." -ForegroundColor Gray
        # --force is used to ensure installation even if it detects a similar version
        winget install --id $dep.Id --silent --accept-package-agreements --accept-source-agreements --force
    }
} else {
    Write-Error "Winget is not installed. Please install App Installer from the Microsoft Store."
    exit 1
}

# 3. Refresh Environment Variables (Robust Method)
Write-Host "Refreshing environment variables..." -ForegroundColor Cyan
foreach($level in "Machine","User") {
   [Environment]::GetEnvironmentVariables($level).GetEnumerator() | ForEach-Object {
       if($_.Name -ne "PSModulePath") { # Avoid breaking PS modules
           [Environment]::SetEnvironmentVariable($_.Name, $_.Value, "Process")
       }
   }
}

# 4. Initialize Zephyr Workspace
# CRITICAL FIX: 'west init' must run before putting other files (like .venv) in the folder
if (-not (Test-Path "$ZephyrPath\.west")) {
    if (Test-Path $ZephyrPath) {
        if ((Get-ChildItem $ZephyrPath).Count -gt 0) {
            Write-Warning "Directory $ZephyrPath exists and is not empty. 'west init' might fail."
            Write-Warning "If the script fails, delete $ZephyrPath and try again."
        }
    }
    
    Write-Host "Initializing Zephyr workspace..." -ForegroundColor Green
    # Install west globally first to bootstrap
    pip install west==$WestVersion
    west init $ZephyrPath --mr $ZephyrVersion
} else {
    Write-Host "Zephyr workspace already initialized." -ForegroundColor Gray
}

Set-Location $ZephyrPath

# 5. Create and Activate Virtual Environment
if (-not (Test-Path "$ZephyrPath\.venv")) {
    Write-Host "Creating virtual environment..." -ForegroundColor Green
    # Use 'python' if 'py' is not available, assuming 3.11 is the default or only python
    if (Get-Command py -ErrorAction SilentlyContinue) {
        py -3.11 -m venv "$ZephyrPath\.venv"
    } else {
        python -m venv "$ZephyrPath\.venv"
    }
}

Write-Host "Activating virtual environment..." -ForegroundColor Green
. "$ZephyrPath\.venv\Scripts\Activate.ps1"

# 6. Update Zephyr
Write-Host "Updating Zephyr and modules..." -ForegroundColor Green
west update

# 7. Export CMake Package
Write-Host "Exporting Zephyr CMake package..." -ForegroundColor Green
west zephyr-export

# 8. Install Python Dependencies
Write-Host "Installing Python dependencies..." -ForegroundColor Green
pip install -r "$ZephyrPath\zephyr\scripts\requirements.txt"
pip install patool

# 9. Install Zephyr SDK
Write-Host "Installing Zephyr SDK $SdkVersion..." -ForegroundColor Green
# This command works after 'west update' fetches the zephyr repo
west sdk install --version $SdkVersion

# 10. Clone K2-Zephyr
if (-not (Test-Path "$ZephyrPath\K2-Zephyr")) {
    Write-Host "Cloning K2-Zephyr repository..." -ForegroundColor Green
    git clone https://github.com/UiASub/K2-Zephyr.git "$ZephyrPath\K2-Zephyr"
}

Write-Host "`nZephyr setup complete!" -ForegroundColor Green
Write-Host "Location: $ZephyrPath" -ForegroundColor Gray
Write-Host "IMPORTANT: Always activate the environment before working:" -ForegroundColor Yellow
Write-Host ". $ZephyrPath\.venv\Scripts\Activate.ps1" -ForegroundColor White