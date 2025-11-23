#!/usr/bin/env bash
# Zephyr RTOS Setup Script for Linux and macOS
# Run with: bash install_zephyr.sh [OPTIONS]
set -euo pipefail  # Exit on error, undefined variables, and pipe failures
# Constants
readonly ZEPHYR_PATH="$HOME/zephyrproject"
readonly WEST_VERSION="1.5.0"
readonly SDK_VERSION="0.17.4"
readonly ZEPHYR_VERSION="v4.2.0"
readonly PYTHON_MIN_VERSION="3.10"
readonly PYTHON_PREFERRED="3.11"
# Global variables
v=0           # Verbosity level
force=0       # Force reinstall flag
update=0      # Update flag
skip_sdk=0    # Skip SDK installation flag
OS=""
PACKAGE_MANAGER=""
# Colors for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly CYAN='\033[0;36m'
readonly NC='\033[0m' # No Color
# Log function with verbosity levels
log() {
    local message="$1"
    local level="${2:-0}"
    if [[ "$v" -ge "$level" ]]; then
        echo -e "$message"
    fi
}
# Error logging
error() {
    echo -e "${RED}ERROR: $1${NC}" >&2
}
# Warning logging
warn() {
    echo -e "${YELLOW}WARNING: $1${NC}" >&2
}
# Info logging
info() {
    echo -e "${GREEN}$1${NC}"
}
# Help description
help() {
cat << 'EOF'
Zephyr RTOS Installation Script
================================
USAGE:
    ./install_zephyr.sh [OPTIONS]
DESCRIPTION:
    Automatically installs and configures Zephyr RTOS development environment
    including Python virtual environment, west tool, Zephyr SDK, and all
    required dependencies for your operating system.
    Supported platforms: macOS (Homebrew), Ubuntu/Debian (apt), Arch/Manjaro (pacman), and Fedora (dnf).
OPTIONS:
    -h, --help, --usage, -?
        Display this help message and exit.
    -v, --verbose
        Enable verbose output. Can be used multiple times to increase verbosity.
        Level 0 (default): Only essential messages
        Level 1 (-v):      Detailed progress information
        Level 2 (-vv):     Very detailed output
        Level 3 (-vvv):    Debug mode with bash trace (set -x)
    -f, --force
        Force reinstallation even if Zephyr is already installed.
        This will recreate the virtual environment and reinstall all components.
        WARNING: This will DELETE the existing ~/zephyrproject directory!
    -u, --update
        Update existing Zephyr installation to the latest version.
        Pulls latest changes from Zephyr repository and updates modules.
    --skip-sdk
        Skip Zephyr SDK installation. Useful if you want to install the SDK
        manually or use a different toolchain.
EXAMPLES:
    # Standard installation
    ./install_zephyr.sh
    # Update existing installation
    ./install_zephyr.sh --update
    # Installation with verbose output
    ./install_zephyr.sh -v
    # Force reinstall with maximum verbosity
    ./install_zephyr.sh -vvv --force
    # Install without SDK (manual SDK setup)
    ./install_zephyr.sh --skip-sdk
REQUIREMENTS:
    macOS:         Homebrew package manager
    Ubuntu/Debian: apt package manager (sudo access required)
    Fedora:        dnf package manager (sudo access required)
    Arch/Manjaro:  pacman package manager (sudo access required)
INSTALLATION PATH:
    ~/zephyrproject/        - Main workspace directory
    ~/zephyrproject/.venv   - Python virtual environment
    ~/zephyrproject/zephyr  - Zephyr RTOS source code
    ~/zephyrproject/K2-Zephyr - K2 Zephyr source code
EXIT CODES:
    0 - Successful completion
    1 - General error or invalid options
    2 - Unsupported operating system
    3 - Installation failure
DOCUMENTATION:
    Zephyr Project: https://docs.zephyrproject.org/
    Getting Started: https://docs.zephyrproject.org/latest/develop/getting_started/
    Zephyr SDK: https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html
    K2 Zephyr: https://github.com/UiASub/K2-Zephyr
EOF
}
# Print help and exit
usage() {
    help
    exit "$1"
}
# Detect operating system and package manager
detect_os() {
    log "${CYAN}Detecting operating system...${NC}" 1
    
    # Check if macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        PACKAGE_MANAGER="brew"
        log "Operating System: macOS" 1
        log "Package Manager: Homebrew" 1
    # Check Linux distribution
    elif [[ -f /etc/os-release ]]; then
        # shellcheck source=/dev/null
        . /etc/os-release
        OS="${ID}"
        
        case "$OS" in
            ubuntu|debian)
                PACKAGE_MANAGER="apt"
                log "Operating System: $OS" 1
                log "Package Manager: apt" 1
                ;;
            arch|manjaro)
                PACKAGE_MANAGER="pacman"
                log "Operating System: $OS" 1
                log "Package Manager: pacman" 1
                ;;
            fedora)
                PACKAGE_MANAGER="dnf"
                log "Operating System: $OS" 1
                log "Package Manager: dnf" 1
                ;;
            *)
                error "Unsupported operating system: $OS"
                error "Supported: Ubuntu, Debian, Arch, Manjaro, Fedora, macOS"
                exit 2
                ;;
        esac
    else
        error "Cannot detect operating system (missing /etc/os-release)"
        exit 2
    fi
}
# Detect available Python version
detect_python() {
    local python_cmd=""
    
    # Try to find Python 3.11 first (preferred), then 3.10+
    # Avoid 3.12+ as it may have compatibility issues with some Zephyr tools
    for cmd in python3.11 python3.10 python3; do
        if command -v "$cmd" &> /dev/null; then
            local version
            version=$("$cmd" --version 2>&1 | grep -oP '\d+\.\d+' | head -1)
            local major minor
            major=$(echo "$version" | cut -d. -f1)
            minor=$(echo "$version" | cut -d. -f2)
            
            # Accept Python 3.10 or 3.11 (avoid 3.12+ for compatibility)
            if [[ "$major" -eq 3 ]] && [[ "$minor" -ge 10 ]] && [[ "$minor" -le 11 ]]; then
                python_cmd="$cmd"
                log "Found Python: $cmd (version $version)" 1
                break
            fi
        fi
    done
    
    if [[ -z "$python_cmd" ]]; then
        error "Python $PYTHON_MIN_VERSION or higher not found"
        exit 3
    fi
    
    echo "$python_cmd"
}
# Check if Zephyr is already installed
check_existing_installation() {
    if [[ -d "$ZEPHYR_PATH/.venv" ]] && [[ "$force" -eq 0 ]] && [[ "$update" -eq 0 ]]; then
        info "Zephyr is already installed at $ZEPHYR_PATH"
        echo "Use --force to reinstall or --update to update"
        log "Zephyr is installed, skipping installation." 1
        exit 0
    elif [[ -d "$ZEPHYR_PATH/.venv" ]] && [[ "$force" -eq 1 ]]; then
        warn "Force reinstall requested. This will DELETE $ZEPHYR_PATH"
        read -rp "Are you sure? Type 'yes' to continue: " confirm
        if [[ "$confirm" != "yes" ]]; then
            echo "Aborted."
            exit 0
        fi
        info "Removing existing installation..."
        rm -rf "$ZEPHYR_PATH"
        log "Removed existing installation" 1
    elif [[ -d "$ZEPHYR_PATH/.venv" ]] && [[ "$update" -eq 1 ]]; then
        info "Updating Zephyr installation..."
        update_zephyr
        exit 0
    fi
}
# Update existing Zephyr installation
update_zephyr() {
    # shellcheck source=/dev/null
    source "$ZEPHYR_PATH/.venv/bin/activate"
    
    if [[ -d "$ZEPHYR_PATH/zephyr" ]]; then
        info "Updating Zephyr repository..."
        (cd "$ZEPHYR_PATH/zephyr" && git pull)
    fi
    
    info "Updating Zephyr modules..."
    (cd "$ZEPHYR_PATH" && west update)
    
    info "Updating Python dependencies..."
    pip install --upgrade pip
    pip install -r "$ZEPHYR_PATH/zephyr/scripts/requirements.txt"
    
    info "Zephyr update complete!"
}
# Install dependencies for APT (Ubuntu/Debian)
install_apt_dependencies() {
    info "Installing dependencies via apt..."
    
    sudo apt-get update
    sudo apt-get upgrade -y
    
    # Try to install Python 3.11 specifically, fall back to python3 if not available
    local python_pkg="python3"
    if apt-cache show python3.11 &>/dev/null; then
        python_pkg="python3.11"
        info "Installing Python 3.11 (recommended for Zephyr)..."
    else
        warn "Python 3.11 not available, using system default python3"
    fi
    
    sudo apt-get install --no-install-recommends -y \
        git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
        "$python_pkg" "${python_pkg}-dev" "${python_pkg}-venv" \
        python3-pip python3-setuptools python3-tk python3-wheel \
        xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
}
# Install dependencies for DNF (Fedora)
install_dnf_dependencies() {
    info "Installing dependencies via dnf..."
    
    sudo dnf upgrade -y
    sudo dnf group install -y "Development Tools" "C Development Tools and Libraries"
    sudo dnf install -y cmake ninja-build gperf dfu-util dtc wget which \
        python3 python3-devel python3-pip python3-tkinter xz file SDL2-devel
}
# Install dependencies for Pacman (Arch/Manjaro)
install_pacman_dependencies() {
    info "Installing dependencies via pacman..."
    
    sudo pacman -Syu --noconfirm
    sudo pacman -S --noconfirm --needed \
        git cmake ninja gperf ccache dfu-util dtc wget \
        python python-pip python-setuptools python-wheel tk xz file make
    
    # Install python-west from AUR
    install_west_from_aur
}
# Install west from AUR (Arch Linux User Repository)
install_west_from_aur() {
    info "Installing west from AUR..."
    
    # Create temporary directory
    local tmp_dir
    tmp_dir=$(mktemp -d)
    
    # Ensure cleanup on exit
    trap 'rm -rf "$tmp_dir"' EXIT
    
    (
        cd "$tmp_dir"
        git clone https://aur.archlinux.org/python-west.git
        cd python-west
        
        warn "Building AUR package python-west. Please review PKGBUILD if concerned."
        log "PKGBUILD location: $tmp_dir/python-west/PKGBUILD" 2
        
        makepkg -si --noconfirm
    )
    
    info "West installed from AUR"
}
# Install dependencies for Homebrew (macOS)
install_brew_dependencies() {
    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        error "Homebrew is not installed. Please install it first:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    info "Installing dependencies via Homebrew..."
    
    # Update Homebrew
    brew update
    
    brew install cmake ninja gperf python@3.12 ccache qemu dtc wget libmagic
    
    # STM32CubeProgrammer is optional
    if brew list --cask stm32cubeprogrammer &> /dev/null || \
       brew install --cask stm32cubeprogrammer 2>/dev/null; then
        log "STM32CubeProgrammer installed" 1
    else
        warn "STM32CubeProgrammer not available via Homebrew (optional)"
    fi
}
# Create Python virtual environment
create_venv() {
    local python_cmd
    python_cmd=$(detect_python)
    
    if [[ ! -d "$ZEPHYR_PATH/.venv" ]]; then
        info "Creating Python virtual environment..."
        "$python_cmd" -m venv "$ZEPHYR_PATH/.venv"
        log "Virtual environment created at $ZEPHYR_PATH/.venv" 1
    else
        log "Virtual environment already exists" 1
    fi
}
# Activate virtual environment and install west
setup_west() {
    info "Activating virtual environment..."
    # shellcheck source=/dev/null
    source "$ZEPHYR_PATH/.venv/bin/activate"
    
    # Upgrade pip
    pip install --upgrade pip
    
    # Install west if not from AUR
    if [[ "$PACKAGE_MANAGER" != "pacman" ]]; then
        if ! command -v west &> /dev/null; then
            info "Installing west $WEST_VERSION..."
            pip install "west==$WEST_VERSION"
        else
            log "West already installed" 1
        fi
    fi
}
# Initialize Zephyr workspace
initialize_workspace() {
    if [[ ! -d "$ZEPHYR_PATH/.west" ]]; then
        info "Initializing Zephyr workspace..."
        
        # Check if directory exists and is not empty
        if [[ -d "$ZEPHYR_PATH" ]] && [[ -n "$(ls -A "$ZEPHYR_PATH" 2>/dev/null)" ]]; then
            # Only .venv should exist at this point
            if [[ "$(ls -A "$ZEPHYR_PATH" | grep -v '^\.venv$' | wc -l)" -gt 0 ]]; then
                warn "Directory $ZEPHYR_PATH is not empty. west init may fail."
            fi
        fi
        
        west init "$ZEPHYR_PATH" --mr "$ZEPHYR_VERSION"
        log "Workspace initialized" 1
    else
        log "Workspace already initialized" 1
    fi
}
# Update Zephyr and modules
update_modules() {
    info "Updating Zephyr and modules (this may take several minutes)..."
    (cd "$ZEPHYR_PATH" && west update)
}
# Export Zephyr CMake package
export_cmake() {
    info "Exporting Zephyr CMake package..."
    (cd "$ZEPHYR_PATH" && west zephyr-export)
}
# Install Python dependencies
install_python_deps() {
    info "Installing Python dependencies..."
    pip install -r "$ZEPHYR_PATH/zephyr/scripts/requirements.txt"
}
# Install Zephyr SDK
install_sdk() {
    if [[ "$skip_sdk" -eq 1 ]]; then
        warn "Skipping SDK installation (--skip-sdk specified)"
        return
    fi
    
    info "Installing Zephyr SDK $SDK_VERSION..."
    info "This may take a while depending on your internet connection..."
    
    (cd "$ZEPHYR_PATH" && west sdk install --version "$SDK_VERSION")
    
    info "SDK installation complete"
}
# Clone K2-Zephyr repository
clone_k2_zephyr() {
    if [[ ! -d "$ZEPHYR_PATH/K2-Zephyr" ]]; then
        info "Cloning K2-Zephyr repository..."
        git clone https://github.com/UiASub/K2-Zephyr.git "$ZEPHYR_PATH/K2-Zephyr"
        log "K2-Zephyr cloned" 1
    else
        log "K2-Zephyr already exists" 1
    fi
}
# Main installation function
install_zephyr() {
    log "${CYAN}Starting Zephyr installation...${NC}" 1
    
    # Install system dependencies based on package manager
    case "$PACKAGE_MANAGER" in
        apt)
            install_apt_dependencies
            ;;
        dnf)
            install_dnf_dependencies
            ;;
        pacman)
            install_pacman_dependencies
            ;;
        brew)
            install_brew_dependencies
            ;;
        *)
            error "Unknown package manager: $PACKAGE_MANAGER"
            exit 3
            ;;
    esac
    
    # CRITICAL: Initialize workspace BEFORE creating venv
    # west init requires an empty directory
    initialize_workspace
    
    # Now create virtual environment
    create_venv
    
    # Setup west and activate venv
    setup_west
    
    # Update Zephyr modules
    update_modules
    
    # Export CMake package
    export_cmake
    
    # Install Python dependencies
    install_python_deps
    
    # Install SDK
    install_sdk
    
    # Clone K2-Zephyr
    clone_k2_zephyr
    
    # Success message
    info "\n========================================="
    info "Zephyr setup complete!"
    info "========================================="
    echo ""
    echo "Installation location: $ZEPHYR_PATH"
    echo "K2-Zephyr location: $ZEPHYR_PATH/K2-Zephyr"
    echo ""
    echo -e "${YELLOW}IMPORTANT:${NC} Activate the virtual environment before working:"
    echo -e "  ${GREEN}source $ZEPHYR_PATH/.venv/bin/activate${NC}"
    echo ""
    echo "To get started:"
    echo "  cd $ZEPHYR_PATH/K2-Zephyr"
    echo ""
}
# Parse command-line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help|--usage|-\?)
                usage 0
                ;;
            -v|--verbose)
                if [[ "$v" -lt 3 ]]; then
                    ((v++))
                fi
                # Enable bash trace at level 3
                if [[ "$v" -eq 3 ]]; then
                    set -x
                fi
                shift
                ;;
            -f|--force)
                force=1
                log "Force reinstall enabled" 1
                shift
                ;;
            --skip-sdk)
                skip_sdk=1
                log "Skipping SDK installation" 1
                shift
                ;;
            -u|--update)
                update=1
                log "Update mode enabled" 1
                shift
                ;;
            *)
                error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
}
# Main execution
main() {
    # Parse arguments
    parse_arguments "$@"
    
    # Detect OS and package manager
    detect_os
    
    # Check for existing installation
    check_existing_installation
    
    # Run installation
    install_zephyr
}
# Run main function with all arguments
main "$@"