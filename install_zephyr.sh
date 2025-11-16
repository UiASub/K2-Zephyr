#!/usr/bin/env bash

#Log function
log ()
{
    if [[ "$v" -ge "$2" ]]; then echo "$1"; fi
}

#Help description for the script
help()
{
echo "Zephyr RTOS Installation Script
================================

USAGE:
    ./install_zephyr.sh [OPTIONS]

DESCRIPTION:
    Automatically installs and configures Zephyr RTOS development environment
    including Python virtual environment, west tool, Zephyr SDK, and all
    required dependencies for your operating system.

    Supported platforms: macOS (Homebrew), Ubuntu/Debian (apt), Arch/Manjaro (pacman) and Fedora (dnf).

OPTIONS:
    -h, --help, --usage, -?
        Display this help message and exit.

    -v, --verbose
        Enable verbose output. Can be used multiple times to increase verbosity.
        Level 0 (default): Only essential messages
        Level 1 (-v):      Detailed progress information
        Level 2 (-vv):     Very detailed output
        Level 3 (-vvv):    Debug mode with bash trace (set -xv)

    -f, --force
        Force reinstallation even if Zephyr is already installed.
        This will recreate the virtual environment and reinstall all components.
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
    macOS:        Homebrew package manager
    Ubuntu/Debian: apt package manager (sudo access required)
    Fedora:      dnf package manager (sudo access required)
    Arch/Manjaro:  pacman package manager (sudo access required)

INSTALLATION PATH:
    ~/zephyrproject/        - Main workspace directory
    ~/zephyrproject/.venv   - Python virtual environment
    ~/zephyrproject/zephyr  - Zephyr RTOS source code
    ~/zephyrproject/K2-Zephyr - K2 Zephyr source code

EXIT CODES:
    0 - Successful completion
    1 - Invalid command-line options
    2 - Argument validation error
    3 - Unsupported operating system

DOCUMENTATION:
    Zephyr Project: https://docs.zephyrproject.org/
    Getting Started: https://docs.zephyrproject.org/latest/develop/getting_started/
    Zephyr SDK: https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html
    K2 Zephyr: https://github.com/UiASub/K2-Zephyr
"
}

#Prints help and exits with error on inncorect usage
usage()  
{  
    help
    exit "$1"
}

#Function to detect package manager of per distrubtion in Linux
detectOs () 
{
    #Checks if macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        PACKAGE_MANAGER="brew"
        log "Operating System = macOS" 1
        log "Package Manager = brew" 1
    #Checks os-release
    elif [ -f /etc/os-release ]; then
    # shellcheck source=/dev/null
        . /etc/os-release
        OS=$ID
        if [[ "$OS" == "ubuntu" || "$OS" == "debian" ]]; then
            PACKAGE_MANAGER="apt"
            log "Package Manager = apt" 1
        elif [[ "$OS" == "arch" || "$OS" == "manjaro" ]]; then
            PACKAGE_MANAGER="pacman"
            log "Package manager = pacman" 1
        elif [[ "$OS" == "fedora" ]]; then
            PACKAGE_MANAGER="dnf"
            log "Package manager = dnf" 1
        else
            #If none of the above is found, exit.
            echo "Unsupported operating system: $OS"
            exit 3
        fi
    else
        echo "Unsupported operating system"
        exit 3
    fi
}


# Check if zephyr is installed
zephyrDetect ()
{
if [ -d ~/zephyrproject/.venv ] && [ "$force" -eq 0 ] && [ "$update" -eq 0 ]; then
    echo "Zephyr is already installed at ~/zephyrproject"
    echo "Use --force to reinstall"
    log "Zephyr is installed, skipping install." 1
elif [ -d ~/zephyrproject/.venv ] && [ "$force" -eq 1 ]; then
    echo "Force reinstall requested. Removing existing installation..."
    rm -rf ~/zephyrproject/.venv
    rm -rf ~/zephyrproject/.west
    log "Installing Zephyr" 0
    installZephyr
elif [ -d ~/zephyrproject/.venv ] && [ "$update" -eq 1 ]; then
    echo "Updating Zephyr installation..."
    cd ~/zephyrproject/zephyr || exit 1
    git pull
    cd ~/zephyrproject || exit 1
    west update
    west packages pip --install
    echo "Zephyr update complete!"
else
    log "Installing Zephyr" 0
    installZephyr
fi
}


# Function to install docker deamon and jq
installZephyr ()
{
    #Finds correct package manager per detectOs function.
    log "Attemting to install zephyr" 1
    case $PACKAGE_MANAGER in
        apt)
            set -euo pipefail
            #  Update and install dependencies for Ubuntu/Debian
            sudo apt-get update && sudo apt-get upgrade -y 
            sudo apt-get install --no-install-recommends git cmake ninja-build gperf \
                ccache dfu-util device-tree-compiler wget \
                python3-dev python3-pip python3-setuptools python3-tk python3-wheel xz-utils file \
                make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1 -y

            # Create virtual environment if it doesn't exist
            if [ ! -d ~/zephyrproject/.venv ]; then
                echo "Creating virtual environment..."
                python3 -m venv ~/zephyrproject/.venv
            fi

            # Activate virtual environment
            source ~/zephyrproject/.venv/bin/activate

            # Install west if not already installed
            if ! command -v west &> /dev/null; then
                echo "Installing west..."
                pip install west
            fi

            # Initialize workspace if not already initialized
            if [ ! -d ~/zephyrproject/.west ]; then
                echo "Initializing Zephyr workspace..."
                west init ~/zephyrproject
            fi

            cd ~/zephyrproject

            # Update Zephyr and modules
            echo "Updating Zephyr and modules..."
            west update

            # Export Zephyr CMake package
            echo "Exporting Zephyr CMake package..."
            west zephyr-export

            # Install Python dependencies
            echo "Installing Python dependencies..."
            west packages pip --install

            # Installing SDK
            if [ "$skip_sdk" -eq 0 ]; then
                echo "Installing Zephyr SDK..."
                cd ~/zephyrproject/zephyr || exit 1
                west sdk install
                cd ~/zephyrproject || exit 1
            else
                echo "Skipping SDK installation (--skip-sdk specified)"
            fi

            if [ ! -d ~/zephyrproject/K2-Zephyr ]; then
                cd ~/zephyrproject || exit 1
                git clone https://github.com/UiASub/K2-Zephyr.git
            fi
            cd ~/zephyrproject/K2-Zephyr || exit 1

            echo "Zephyr setup complete! Go to ~/zephyrproject/K2-Zephyr to start working."
            echo "Remember to activate ~/zephyrproject/.venv/bin/activate when you start a new terminal session."
            ;;
        fedora)
            set -euo pipefail
            sudo dnf upgrade -y

            # Dependencies installation for Fedora
            sudo dnf group install "Development Tools" "C Development Tools and Libraries" -y
            sudo dnf install cmake ninja-build gperf dfu-util dtc wget which \
              python3-pip python3-tkinter xz file python3-devel SDL2-devel -y

            # Create virtual environment if it doesn't exist
            if [ ! -d ~/zephyrproject/.venv ]; then
                echo "Creating virtual environment..."
                python3 -m venv ~/zephyrproject/.venv
            fi

            # Activate virtual environment
            source ~/zephyrproject/.venv/bin/activate

            # Install west if not already installed
            if ! command -v west &> /dev/null; then
                echo "Installing west..."
                pip install west
            fi

            # Initialize workspace if not already initialized
            if [ ! -d ~/zephyrproject/.west ]; then
                echo "Initializing Zephyr workspace..."
                west init ~/zephyrproject
            fi

            cd ~/zephyrproject || exit 1

            # Update Zephyr and modules
            echo "Updating Zephyr and modules..."
            west update

            # Export Zephyr CMake package
            echo "Exporting Zephyr CMake package..."
            west zephyr-export

            # Install Python dependencies
            echo "Installing Python dependencies..."
            west packages pip --install

            # Installing SDK
            if [ "$skip_sdk" -eq 0 ]; then
                echo "Installing Zephyr SDK..."
                cd ~/zephyrproject/zephyr || exit 1
                west sdk install
                cd ~/zephyrproject || exit 1
            else
                echo "Skipping SDK installation (--skip-sdk specified)"
            fi

            if [ ! -d ~/zephyrproject/K2-Zephyr ]; then
                cd ~/zephyrproject || exit 1
                git clone https://github.com/UiASub/K2-Zephyr.git
            fi
            cd ~/zephyrproject/K2-Zephyr || exit 1
            
            echo "Zephyr setup complete! Go to ~/zephyrproject/K2-Zephyr to start working."
            ;;
        pacman)
            set -euo pipefail
            # Update and install dependencies for Arch/Manjaro
            sudo pacman -Syu -y
            sudo pacman -S git cmake ninja gperf ccache dfu-util dtc wget \
            python-pip python-setuptools python-wheel tk xz file make -y 
            
            cd ~
            mkdir tmp && cd tmp || exit 1
            git clone https://aur.archlinux.org/python-west.git

            # Create the Install package
            cd python-west && makepkg -s || exit 1

            # Install the package
            sudo pacman -U python-west*.tar.xz --noconfirm

            # finally some cleanup
            cd ~ || exit 1
            rm -rf tmp

            # Create virtual environment if it doesn't exist
            if [ ! -d ~/zephyrproject/.venv ]; then
                echo "Creating virtual environment..."
                python3 -m venv ~/zephyrproject/.venv
            fi

            # Activate virtual environment
            source ~/zephyrproject/.venv/bin/activate

            # Initialize workspace if not already initialized
            if [ ! -d ~/zephyrproject/.west ]; then
                echo "Initializing Zephyr workspace..."
                west init ~/zephyrproject
            fi

            cd ~/zephyrproject || exit 1

            # Update Zephyr and modules
            echo "Updating Zephyr and modules..."
            west update

            # Export Zephyr CMake package
            echo "Exporting Zephyr CMake package..."
            west zephyr-export

            # Install Python dependencies
            echo "Installing Python dependencies..."
            west packages pip --install

            # Installing SDK
            if [ "$skip_sdk" -eq 0 ]; then
                echo "Installing Zephyr SDK..."
                cd ~/zephyrproject/zephyr || exit 1
                west sdk install
                cd ~/zephyrproject || exit 1
            else
                echo "Skipping SDK installation (--skip-sdk specified)"
            fi

            if [ ! -d ~/zephyrproject/K2-Zephyr ]; then
                cd ~/zephyrproject || exit 1
                git clone https://github.com/UiASub/K2-Zephyr.git
            fi
            cd ~/zephyrproject/K2-Zephyr || exit 1
            
            echo "Zephyr setup complete!"
            ;;
        brew)
            # Check if Homebrew is installed
            if ! command -v brew &> /dev/null; then
                echo "Homebrew is not installed. Please install it first:"
                echo "/bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
                exit 1
            fi

            set -euo pipefail
            
            # Install dependencies via Homebrew
            log "Installing dependencies via Homebrew..." 1
            brew install cmake ninja gperf python3 ccache qemu dtc wget libmagic
            
            # Create virtual environment if it doesn't exist
            if [ ! -d ~/zephyrproject/.venv ]; then
                echo "Creating virtual environment..."
                python3 -m venv ~/zephyrproject/.venv
            fi

            # Activate virtual environment
            source ~/zephyrproject/.venv/bin/activate

            # Install west if not already installed
            if ! command -v west &> /dev/null; then
                echo "Installing west..."
                pip install west
            fi

            # Initialize workspace if not already initialized
            if [ ! -d ~/zephyrproject/.west ]; then
                echo "Initializing Zephyr workspace..."
                west init ~/zephyrproject
            fi

            cd ~/zephyrproject

            # Update Zephyr and modules
            echo "Updating Zephyr and modules..."
            west update

            # Export Zephyr CMake package
            echo "Exporting Zephyr CMake package..."
            west zephyr-export

            # Install Python dependencies
            echo "Installing Python dependencies..."
            west packages pip --install

            # Installing SDK
            if [ "$skip_sdk" -eq 0 ]; then
                echo "Installing Zephyr SDK..."
                cd ~/zephyrproject/zephyr || exit 1
                west sdk install
                cd ~/zephyrproject || exit 1
            else
                echo "Skipping SDK installation (--skip-sdk specified)"
            fi

            if [ ! -d ~/zephyrproject/K2-Zephyr ]; then
                cd ~/zephyrproject || exit 1
                git clone https://github.com/UiASub/K2-Zephyr.git
            fi
            cd ~/zephyrproject/K2-Zephyr || exit 1

            echo "Zephyr setup complete! Go to ~/zephyrproject/K2-Zephyr to start working."
            ;;
    esac

}


# Initialize flags
v=0           # Verbosity level
force=0       # Force reinstall flag
update=0      # Update flag
skip_sdk=0    # Skip SDK installation flag

# Simple argument parsing (compatible with both GNU and BSD)
while [[ $# -gt 0 ]]; do
    case "$1" in
    -h|--help|--usage|-\?)
        usage 0;;
    -v|--verbose)
        if [[ "$v" -lt 3 ]]; then ((v++)); fi
        # Enable bash trace at level 3
        if [[ "$v" -eq 3 ]]; then set -xv; fi
        shift;;
    -f|--force)
        force=1
        log "Force reinstall enabled" 1
        shift;;
    --skip-sdk)
        skip_sdk=1
        log "Skipping SDK installation" 1
        shift;;
    -u|--update)
        update=1
        log "Update Zephyr enabled" 1
        shift;;
    *)
        echo "Unknown option: $1"
        echo "Use --help for usage information"
        exit 1;;
    esac
done

detectOs
zephyrDetect
exit 0
