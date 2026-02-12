# Quick Start Guide - Steam Deck Build

This guide will help you build and install Moonlight on your Steam Deck with just one command.

## New Features

### 🔄 Auto-Reconnect
- **Automatic reconnection** when connection is lost
- **2 minutes of retry attempts** before showing error
- **Visual feedback** showing reconnection progress
- **Cancel anytime** with Ctrl+Alt+C

### 🎮 Steam Deck Optimized
- Full controller support
- Hardware acceleration
- Optimized for Steam Deck display
- Simple one-command build

## Building for Steam Deck

### Option 1: One-Command Build (Recommended)

```bash
make steamdeck
```

That's it! The script will:
1. Check and install dependencies
2. Setup Flatpak runtimes
3. Build the Flatpak package
4. Show installation instructions

### Option 2: Manual Build

```bash
# Install dependencies
make install

# Build the Flatpak
make build
```

## Installation on Steam Deck

After building, you'll get a file like `moonlight-steamdeck-*.flatpak`.

### Method 1: Direct Installation (if building on Steam Deck)
```bash
flatpak install --user moonlight-steamdeck-*.flatpak
```

### Method 2: Via Discover App
1. Copy the `.flatpak` file to your Steam Deck
2. Open Discover app
3. Click on the file to install

### Method 3: Add to Steam
1. Install using Method 1 or 2
2. Open Steam in Desktop Mode
3. Go to: Games → Add a Non-Steam Game
4. Browse to `/usr/bin/flatpak`
5. Set launch options: `run com.moonlight_stream.Moonlight`

## Features

- ✅ **Auto-reconnect for 2 minutes** when connection fails
- ✅ **Steam Deck controller support** with virtual gamepad
- ✅ **Hardware acceleration** for smooth streaming
- ✅ **4K HDR support** for compatible displays
- ✅ **Low latency streaming** optimized for gaming
- ✅ **5.1/7.1 surround sound** support

## Using Auto-Reconnect

When your connection is lost:
1. Moonlight will automatically start reconnecting
2. You'll see: "Connection lost. Reconnecting... (1/24)"
3. It will retry for approximately 2 minutes
4. Press **Ctrl+Alt+C** to cancel if needed

## Troubleshooting

### Build Issues

```bash
# Clean and rebuild
make clean
make steamdeck
```

### Installation Issues

```bash
# Install as user if system install fails
flatpak install --user moonlight-steamdeck-*.flatpak
```

### Runtime Issues

```bash
# Add Flathub repository
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

# Install runtimes
flatpak install flathub org.kde.Platform//23.08
```

## System Requirements

- Steam Deck (or compatible Linux system)
- Network connection to host PC
- Host PC running GeForce Experience or Sunshine
- At least 2GB free space for build

## Support

For issues or questions:
- GitHub: https://github.com/moonlight-stream/moonlight-qt
- Steam Deck: https://help.steampowered.com/

## What's Different?

### Auto-Reconnect Changes
- Default retry attempts: 3 → 24 (approximately 2 minutes)
- Shows reconnecting status in UI instead of immediate error
- Allows user to cancel with Ctrl+Alt+C
- Displays attempt counter: "Reconnecting... (5/24)"

### Build Simplification
- One command builds everything: `make steamdeck`
- Automatic dependency checking and installation
- Clear, colorful output and instructions
- Includes all Steam Deck optimizations

Enjoy streaming with Moonlight on your Steam Deck! 🎮
