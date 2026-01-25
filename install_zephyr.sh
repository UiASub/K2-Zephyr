#!/usr/bin/env bash
# Zephyr RTOS Setup Script for Linux and macOS
# Run with: bash install_zephyr.sh [OPTIONS]
set -euo pipefail  # Exit on error, undefined variables, and pipe failures

# Constants
readonly ZEPHYR_PATH="$HOME/zephyrproject"
readonly ZEPHYR_VERSION="v4.2.0"
readonly PYTHON_VERSION="3.11"

# Global variables
v=0           # Verbosity level
force=0       # Force reinstall flag
update=0      # Update flag
skip_sdk=0    # Skip SDK installation flag
use_pip=0     # Use pip instead of uv flag
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
    Supported platforms: macOS (Homebrew), Ubuntu/Debian (apt), and Fedora (dnf).
OPTIONS:
    -h, --help, --usage, -?
        Display this help message and exit.
    -v,
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
        Update an existing Zephyr installation (pull Zephyr repo changes and run west update).
        Updates modules and Python dependencies for the current workspace/manifest.
    --skip-sdk
        Skip Zephyr SDK installation. Useful if you want to install the SDK
        manually or use a different toolchain.
    --pip
        Use pip instead of uv for Python package management.
        By default, the script uses uv which is faster and more reliable.
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
    # Use pip instead of uv
    ./install_zephyr.sh --pip
REQUIREMENTS:
    macOS:         Homebrew package manager
    Ubuntu/Debian: apt package manager (sudo access required)
    Fedora:        dnf package manager (sudo access required)
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
    Getting Started: https://docs.zephyrproject.org/4.2.0/develop/getting_started/
    Zephyr SDK: https://docs.zephyrproject.org/4.2.0/develop/toolchains/zephyr_sdk.html
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
            ubuntu|debian|linuxmint)
                PACKAGE_MANAGER="apt"
                log "Operating System: $OS" 1
                log "Package Manager: apt" 1
                ;;
            fedora)
                PACKAGE_MANAGER="dnf"
                log "Operating System: $OS" 1
                log "Package Manager: dnf" 1
                ;;
            *)
                error "Unsupported operating system: $OS"
                error "Supported: Ubuntu, Debian, Linux Mint, Fedora, and macOS"
                exit 2
                ;;
        esac
    else
        error "Cannot detect operating system (missing /etc/os-release)"
        exit 2
    fi
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

# Install uv if not present
install_uv() {
  if [[ "$use_pip" -eq 1 ]]; then
    log "Skipping uv installation (--pip specified)" 1
    return
  fi

  info "Ensuring uv is installed..."

  if ! command -v uv >/dev/null 2>&1; then
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
    hash -r
  fi

  if ! command -v uv >/dev/null 2>&1; then
    error "uv installation failed"
    exit 3
  fi

  log "uv is available" 1
}

# Update existing Zephyr installation
update_zephyr() {
    # shellcheck source=/dev/null
    source "$ZEPHYR_PATH/.venv/bin/activate"

    info "Zephyr is pinned to $ZEPHYR_VERSION; skipping git pull"

    pushd "$ZEPHYR_PATH" > /dev/null

    info "Updating Zephyr modules..."
    west update

    info "Updating Python dependencies..."
    west packages pip --install

    popd > /dev/null

    info "Zephyr update complete!"
}

# Install dependencies for APT (Ubuntu/Debian)
install_apt_dependencies() {
    info "Installing dependencies via apt..."

    sudo apt-get update
    sudo apt-get upgrade -y
    sudo apt-get install --no-install-recommends -y \
        git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget \
        xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev \
        libmagic1 python3-dev python3-venv python3-tk openocd
}

# Install dependencies for DNF (Fedora)
install_dnf_dependencies() {
    info "Installing dependencies via dnf..."

    sudo dnf upgrade -y
    sudo dnf group install development-tools -y
    sudo dnf install -y git cmake ninja-build gperf ccache dfu-util dtc wget \
        which xz file make gcc gcc-multilib g++ SDL2-devel \
        file-devel python3-devel python3-tkinter openocd
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

    brew install cmake ninja gperf python3 python-tk ccache qemu dtc libmagic wget openocd

# Create Python virtual environment
create_venv() {
  mkdir -p "$ZEPHYR_PATH"

  if [[ "$use_pip" -eq 1 ]]; then
    info "Creating virtual environment with pip..."
    python$PYTHON_VERSION -m venv "$ZEPHYR_PATH/.venv"
  else
    # ensure that Python is present (uv will download if needed)
    uv python install "$PYTHON_VERSION"

    # pin per-workspace (creates/updates .python-version)
    (cd "$ZEPHYR_PATH" && uv python pin "$PYTHON_VERSION")

    # create venv with explicit Python version
    uv venv "$ZEPHYR_PATH/.venv" --python "$PYTHON_VERSION"
  fi
}
# Activate virtual environment and install west
setup_west() {
    info "Installing west in virtual environment..."
    # shellcheck source=/dev/null
    source "$ZEPHYR_PATH/.venv/bin/activate"
    
    if [[ "$use_pip" -eq 1 ]]; then
        pip install west
    else
        # Install pip module so 'python -m pip' works (needed by west packages pip)
        uv pip install pip
        uv pip install west
    fi
    log "west installed in venv" 1
}
# Initialize Zephyr workspace
initialize_workspace() {
    if [[ ! -d "$ZEPHYR_PATH/.west" ]]; then
        info "Initializing Zephyr workspace..."

        west init "$ZEPHYR_PATH" --mr "$ZEPHYR_VERSION"
        pushd "$ZEPHYR_PATH" > /dev/null
        west update
        popd > /dev/null
        log "Workspace initialized" 1
    else
        log "Workspace already initialized" 1
    fi
}
# Export Zephyr CMake package
export_cmake() {
    info "Exporting Zephyr CMake package..."
    pushd "$ZEPHYR_PATH" > /dev/null
    west zephyr-export
    popd > /dev/null
}
# Install Python dependencies via west
install_python_deps() {
    info "Installing Python dependencies via west..."
    pushd "$ZEPHYR_PATH" > /dev/null
    west packages pip --install
    popd > /dev/null
    log "Python dependencies installed" 1
}
# Install Zephyr SDK
install_sdk() {
    if [[ "$skip_sdk" -eq 1 ]]; then
        warn "Skipping SDK installation (--skip-sdk specified)"
        return
    fi

    info "Installing Zephyr SDK with ARM toolchain..."
    info "This may take a while depending on your internet connection..."

    pushd "$ZEPHYR_PATH/zephyr" > /dev/null
    west sdk install --toolchains arm-zephyr-eabi
    popd > /dev/null

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

# Configure default board for nucleo_f767zi
configure_board() {
    info "Configuring default board: nucleo_f767zi..."
    pushd "$ZEPHYR_PATH" > /dev/null
    west config --local build.board nucleo_f767zi
    popd > /dev/null
    log "Board configured: nucleo_f767zi" 1
}

# Build K2-Zephyr project
build_k2_zephyr() {
    info "Building K2-Zephyr project..."
    pushd "$ZEPHYR_PATH/K2-Zephyr" > /dev/null
    west build -p always
    popd > /dev/null
    info "K2-Zephyr build complete!"
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
        brew)
            install_brew_dependencies
            ;;
        *)
            error "Unknown package manager: $PACKAGE_MANAGER"
            exit 3
            ;;
    esac

    # 1. Ensure uv is installed
    install_uv

    # 2. Create virtual environment
    create_venv

    # 3. Activate venv and install west
    setup_west

    # 4. west init and west update
    initialize_workspace

    # 5. west zephyr-export
    export_cmake

    # 6. west packages pip --install
    install_python_deps

    # Install SDK
    install_sdk

    # Clone K2-Zephyr
    clone_k2_zephyr

    # Configure board
    configure_board

    # Build K2-Zephyr
    build_k2_zephyr

    # Success message
    info "\n========================================="
    info "Zephyr setup complete!"
    info "========================================="
    echo ""
    echo -e "${YELLOW}Remember to install STM32CubeProgrammer for faster flashing. OpenOCD works as an alternative.${NC}"
    echo ""
    echo "Installation location: $ZEPHYR_PATH"
    echo "K2-Zephyr location: $ZEPHYR_PATH/K2-Zephyr"
    echo ""
    echo -e "${YELLOW}IMPORTANT:${NC} Activate the virtual environment before working:"
    echo -e "  ${GREEN}source $ZEPHYR_PATH/.venv/bin/activate${NC}"
    echo ""
    echo -e "${YELLOW}Note:${NC} If you use VSCode, make sure to read the README in K2-Zephyr/.vscode for proper setup."
    echo ""
    echo "To get started:"
    echo -e "  ${CYAN}cd $ZEPHYR_PATH/K2-Zephyr${NC}"
    echo -e "  ${GREEN}./build ${NC}"
    echo -e "  ${GREEN}west flash${NC} to flash the board"
    echo ""
}
# Parse command-line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help|--usage|-\?)
                usage 0
                ;;
            -v*)
                local count=${#1}
                ((count--))
                v=$((v + count))
                ((v > 3)) && v=3
                # Enable bash trace at level 3
                if [[ "$v" -eq 3 ]]; then
                  log "Debug tracing enabled"
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
            --pip)
                use_pip=1
                log "Using pip instead of uv" 1
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