#!/bin/bash

# Moonlight Steam Deck Flatpak Build Script with Versioning
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to get version from git
get_version() {
    # Try to get version from git tag
    local git_version=$(git describe --tags --abbrev=0 2>/dev/null || echo "v6.1.0")
    local git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    local build_date=$(date +%Y%m%d)
    
    # Remove 'v' prefix if present
    git_version=${git_version#v}
    
    echo "${git_version}-${git_commit}-${build_date}"
}

# Function to check dependencies
check_dependencies() {
    print_status "Checking dependencies..."
    
    local missing_deps=()
    
    if ! command -v flatpak-builder &> /dev/null; then
        missing_deps+=("flatpak-builder")
    fi
    
    if ! command -v flatpak &> /dev/null; then
        missing_deps+=("flatpak")
    fi
    
    if ! command -v git &> /dev/null; then
        missing_deps+=("git")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        echo "Please install them with:"
        echo "sudo apt update && sudo apt install ${missing_deps[*]}"
        exit 1
    fi
    
    print_success "All dependencies found"
}

# Function to setup Flatpak runtimes
setup_runtimes() {
    print_status "Setting up Flatpak runtimes..."
    
    # Add Flathub if not already added
    if ! flatpak remote-list | grep -q flathub; then
        print_status "Adding Flathub repository..."
        flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo || true
    fi
    
    # Install required runtimes (skip if system bus unavailable)
    print_status "Installing required runtimes..."
    if [ -S /run/dbus/system_bus_socket ] 2>/dev/null; then
        flatpak install --user org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08 || true
    else
        print_warning "System bus unavailable, skipping runtime installation"
        print_warning "This may cause build issues. Consider running in a proper desktop environment."
        
        # Try to download runtimes manually if possible
        print_status "Attempting to download runtimes manually..."
        flatpak-builder --download-only --force-clean --user --install-deps-from=flathub /tmp/test-build moonlight-steamdeck.yml 2>/dev/null || true
    fi
    
    print_success "Flatpak runtimes setup complete"
}

# Function to build Flatpak
build_flatpak() {
    local version=$1
    local build_dir="build-steamdeck-${version}"
    local repo_dir="repo-${version}"
    local output_file="moonlight-steamdeck-${version}.flatpak"
    
    print_status "Building Flatpak for version: ${version}"
    
    # Clean previous builds
    if [ -d "$build_dir" ]; then
        print_status "Cleaning previous build directory..."
        rm -rf "$build_dir"
    fi
    
    if [ -d "$repo_dir" ]; then
        print_status "Cleaning previous repo directory..."
        rm -rf "$repo_dir"
    fi
    
    # Create build directory
    mkdir -p "$build_dir"
    
    # Copy manifest
    cp moonlight-steamdeck.yml "$build_dir/"
    
    # Build the Flatpak
    print_status "Building Flatpak package..."
    flatpak-builder \
        --force-clean \
        --user \
        --install-deps-from=flathub \
        --repo="$repo_dir" \
        "$build_dir" \
        "$build_dir/moonlight-steamdeck.yml"
    
    # Create Flatpak bundle
    print_status "Creating Flatpak bundle..."
    flatpak build-bundle "$repo_dir" "$output_file" com.moonlight_stream.Moonlight
    
    print_success "Build complete! Output: $output_file"
    
    # Show file size
    local file_size=$(du -h "$output_file" | cut -f1)
    print_status "Bundle size: $file_size"
    
    echo "$output_file"
}

# Function to create release notes
create_release_notes() {
    local version=$1
    local output_file=$2
    
    cat > "RELEASE_NOTES_${version}.md" << EOF
# Moonlight Steam Deck Flatpak v${version}

## Release Information
- **Version**: ${version}
- **Build Date**: $(date)
- **Git Commit**: $(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
- **Git Tag**: $(git describe --tags --abbrev=0 2>/dev/null || echo "none")

## Installation Instructions

### Method 1: Via Terminal
\`\`\`bash
flatpak install ${output_file}
\`\`\`

### Method 2: Via Discover App
1. Copy \`${output_file}\` to your Steam Deck
2. Open Discover app
3. Click "Install" on the Flatpak file

### Method 3: Add to Steam
1. Open Steam in Desktop Mode
2. Go to Games → Add a Non-Steam Game to My Library
3. Browse to \`/usr/bin/flatpak\`
4. Set launch options to: \`run com.moonlight_stream.Moonlight\`

## Steam Deck Optimizations
- Enhanced gamepad support with Steam Deck controllers
- Full hardware access for optimal performance
- Steam integration for virtual gamepad support
- Optimized for Steam Deck's display and audio systems

## Features
- Game streaming from NVIDIA GeForce Experience or Sunshine
- Low latency streaming optimized for gaming
- Full controller support
- 4K HDR streaming support
- Audio streaming with 5.1/7.1 surround sound

## System Requirements
- Steam Deck (or compatible Linux system)
- Network connection to host PC
- Host PC running GeForce Experience or Sunshine

## Troubleshooting
If you encounter issues:
1. Ensure your host PC is running GeForce Experience or Sunshine
2. Check network connectivity between devices
3. Verify firewall settings allow Moonlight traffic
4. Try running in Desktop Mode if Game Mode has issues

## Support
For support, visit: https://github.com/moonlight-stream/moonlight-qt
EOF

    print_success "Release notes created: RELEASE_NOTES_${version}.md"
}

# Main execution
main() {
    print_status "Starting Moonlight Steam Deck Flatpak build..."
    
    # Check dependencies
    check_dependencies
    
    # Setup runtimes
    setup_runtimes
    
    # Get version
    local version=$(get_version)
    print_status "Building version: $version"
    
    # Build Flatpak
    local output_file=$(build_flatpak "$version")
    
    # Create release notes
    create_release_notes "$version" "$output_file"
    
    print_success "Build process completed successfully!"
    print_status "Files created:"
    echo "  - $output_file"
    echo "  - RELEASE_NOTES_${version}.md"
    echo ""
    print_status "To install on Steam Deck:"
    echo "  flatpak install $output_file"
}

# Run main function
main "$@"