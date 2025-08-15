#!/bin/bash

# Local Moonlight Steam Deck Flatpak Build Script
# This version attempts to work in limited environments

set -e

echo "Building Moonlight Steam Deck Flatpak (Local Mode)..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# Get version
VERSION="6.1.1-$(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')-$(date +%Y%m%d)"
print_status "Building version: $VERSION"

# Create build directory
BUILD_DIR="build-local-${VERSION}"
mkdir -p "$BUILD_DIR"

# Copy manifest
cp moonlight-steamdeck.yml "$BUILD_DIR/"

# Try to add flathub remote if not exists
print_status "Setting up Flathub remote..."
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo 2>/dev/null || true

# Try to install runtimes
print_status "Installing required runtimes..."
flatpak install --user org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08 2>/dev/null || {
    print_warning "Could not install runtimes automatically"
    print_warning "This is expected in this environment"
}

# Attempt build
print_status "Attempting Flatpak build..."
if flatpak-builder \
    --force-clean \
    --user \
    --install-deps-from=flathub \
    --repo=repo-local \
    "$BUILD_DIR" \
    "$BUILD_DIR/moonlight-steamdeck.yml"; then
    
    print_success "Build completed successfully!"
    
    # Create bundle
    print_status "Creating Flatpak bundle..."
    OUTPUT_FILE="moonlight-steamdeck-${VERSION}.flatpak"
    flatpak build-bundle repo-local "$OUTPUT_FILE" com.moonlight_stream.Moonlight
    
    print_success "Bundle created: $OUTPUT_FILE"
    
    # Show file size
    if [ -f "$OUTPUT_FILE" ]; then
        SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
        print_status "Bundle size: $SIZE"
    fi
    
    # Create release notes
    cat > "RELEASE_NOTES_${VERSION}.md" << EOF
# Moonlight Steam Deck Flatpak v${VERSION}

## Release Information
- **Version**: ${VERSION}
- **Build Date**: $(date)
- **Git Commit**: $(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
- **Build Type**: Local Build

## Installation Instructions

### Method 1: Via Terminal
\`\`\`bash
flatpak install ${OUTPUT_FILE}
\`\`\`

### Method 2: Via Discover App
1. Copy \`${OUTPUT_FILE}\` to your Steam Deck
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

## Support
For support, visit: https://github.com/moonlight-stream/moonlight-qt
EOF

    print_success "Release notes created: RELEASE_NOTES_${VERSION}.md"
    
else
    print_error "Build failed"
    print_warning "This is expected in this environment without proper desktop setup"
    print_status "The build system is ready for GitHub Actions or proper desktop environment"
    exit 1
fi