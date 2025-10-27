#!/bin/bash
# Simple Steam Deck Build Script for Moonlight
# This script automates the entire process of building and creating an installable Flatpak

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Moonlight Steam Deck - Simple Build Script         ║${NC}"
echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo ""

# Check if we're on Linux
if [ "$(uname)" != "Linux" ]; then
    echo -e "${RED}Error: This script must be run on Linux${NC}"
    exit 1
fi

# Function to check and install dependencies
check_dependencies() {
    echo -e "${BLUE}[1/4] Checking dependencies...${NC}"
    
    local missing_deps=()
    
    if ! command -v flatpak &> /dev/null; then
        missing_deps+=("flatpak")
    fi
    
    if ! command -v flatpak-builder &> /dev/null; then
        missing_deps+=("flatpak-builder")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo -e "${YELLOW}Missing dependencies: ${missing_deps[*]}${NC}"
        echo -e "${YELLOW}Installing dependencies...${NC}"
        
        # Try to install automatically
        if command -v apt &> /dev/null; then
            sudo apt update
            sudo apt install -y "${missing_deps[@]}"
        elif command -v dnf &> /dev/null; then
            sudo dnf install -y "${missing_deps[@]}"
        elif command -v pacman &> /dev/null; then
            sudo pacman -S --noconfirm "${missing_deps[@]}"
        else
            echo -e "${RED}Error: Could not determine package manager${NC}"
            echo -e "${YELLOW}Please install manually: ${missing_deps[*]}${NC}"
            exit 1
        fi
    fi
    
    echo -e "${GREEN}✓ All dependencies installed${NC}"
}

# Function to setup Flatpak runtimes
setup_runtimes() {
    echo -e "${BLUE}[2/4] Setting up Flatpak runtimes...${NC}"
    
    # Add Flathub if not already added
    if ! flatpak remote-list | grep -q flathub; then
        echo "Adding Flathub repository..."
        flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    fi
    
    echo -e "${GREEN}✓ Flatpak runtimes ready${NC}"
}

# Function to build Flatpak
build_flatpak() {
    echo -e "${BLUE}[3/4] Building Flatpak...${NC}"
    echo -e "${YELLOW}This may take 10-30 minutes depending on your system...${NC}"
    
    # Run the build script
    if [ -f "./scripts/build-steamdeck-flatpak.sh" ]; then
        ./scripts/build-steamdeck-flatpak.sh
    else
        echo -e "${RED}Error: Build script not found${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Build complete${NC}"
}

# Function to show installation instructions
show_instructions() {
    echo -e "${BLUE}[4/4] Installation Instructions${NC}"
    echo ""
    
    # Find the generated flatpak file
    local flatpak_file=$(ls moonlight-steamdeck-*.flatpak 2>/dev/null | head -n 1)
    
    if [ -n "$flatpak_file" ]; then
        local file_size=$(du -h "$flatpak_file" | cut -f1)
        
        echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
        echo -e "${GREEN}Build successful!${NC}"
        echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
        echo ""
        echo -e "Generated file: ${GREEN}$flatpak_file${NC} (${file_size})"
        echo ""
        echo -e "${YELLOW}To install on your Steam Deck:${NC}"
        echo ""
        echo -e "${BLUE}Option 1: Direct Installation (if building on Steam Deck)${NC}"
        echo "  flatpak install --user $flatpak_file"
        echo ""
        echo -e "${BLUE}Option 2: Copy to Steam Deck${NC}"
        echo "  1. Copy '$flatpak_file' to your Steam Deck"
        echo "  2. On Steam Deck, open Discover app"
        echo "  3. Click 'Install' on the Flatpak file"
        echo ""
        echo -e "${BLUE}Option 3: Add to Steam${NC}"
        echo "  1. Install using Option 1 or 2"
        echo "  2. Open Steam in Desktop Mode"
        echo "  3. Games → Add a Non-Steam Game"
        echo "  4. Browse to '/usr/bin/flatpak'"
        echo "  5. Set launch options: 'run com.moonlight_stream.Moonlight'"
        echo ""
        echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
        echo ""
        echo -e "${YELLOW}Features included:${NC}"
        echo "  ✓ Auto-reconnect for 2 minutes if connection fails"
        echo "  ✓ Steam Deck controller support"
        echo "  ✓ Hardware acceleration"
        echo "  ✓ 4K HDR streaming support"
        echo ""
    else
        echo -e "${RED}Error: No Flatpak file found${NC}"
        exit 1
    fi
}

# Main execution
main() {
    check_dependencies
    setup_runtimes
    build_flatpak
    show_instructions
}

# Run main function
main "$@"
