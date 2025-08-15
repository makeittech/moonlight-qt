# Moonlight Steam Deck Flatpak Build

This directory contains the build system for creating a Steam Deck optimized Flatpak version of Moonlight.

## Quick Start

### Prerequisites

```bash
# Install build dependencies
make install

# Or manually:
sudo apt update
sudo apt install -y flatpak-builder flatpak git
```

### Building

```bash
# Build the Flatpak
make build

# Or use the script directly:
./scripts/build-steamdeck-flatpak.sh
```

### Installation on Steam Deck

1. **Via Terminal**:
   ```bash
   flatpak install moonlight-steamdeck-*.flatpak
   ```

2. **Via Discover App**:
   - Copy the `.flatpak` file to your Steam Deck
   - Open Discover app
   - Click "Install" on the Flatpak file

3. **Add to Steam**:
   - Open Steam in Desktop Mode
   - Go to Games → Add a Non-Steam Game to My Library
   - Browse to `/usr/bin/flatpak`
   - Set launch options to: `run com.moonlight_stream.Moonlight`

## Build System

### Available Make Targets

- `make build` - Build the Steam Deck Flatpak
- `make clean` - Clean build artifacts
- `make install` - Install build dependencies
- `make test` - Test the build
- `make release` - Create a new release
- `make help` - Show help message

### Automated Builds

The repository includes GitHub Actions workflows that automatically build Flatpaks on:

- **Tag pushes** - Creates releases automatically
- **Main branch pushes** - Builds for testing
- **Pull requests** - Validates builds
- **Manual triggers** - Custom version builds

## Steam Deck Optimizations

### Hardware Access
- Full DRI access for hardware acceleration
- Input device access for controllers
- Audio device access for optimal sound

### Steam Integration
- Steam virtual gamepad support
- Enhanced SDL controller handling
- Steam Deck specific environment variables

### Performance
- Optimized for Steam Deck's hardware
- Reduced latency for gaming
- Efficient resource usage

## Versioning

The build system uses semantic versioning with the following format:
```
{git_tag}-{git_commit}-{build_date}
```

Example: `6.1.0-a1b2c3d-20231201`

## File Structure

```
├── moonlight-steamdeck.yml          # Flatpak manifest
├── scripts/
│   └── build-steamdeck-flatpak.sh   # Build script
├── .github/workflows/
│   └── build-steamdeck-flatpak.yml  # GitHub Actions workflow
├── Makefile                         # Build automation
└── STEAMDECK_BUILD.md              # This file
```

## Troubleshooting

### Build Issues

1. **Missing dependencies**:
   ```bash
   make install
   ```

2. **Flatpak runtime issues**:
   ```bash
   flatpak install --user org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08
   ```

3. **Permission issues**:
   ```bash
   sudo usermod -a -G flatpak $USER
   # Log out and back in
   ```

### Installation Issues

1. **Flatpak not found**:
   ```bash
   flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
   ```

2. **Permission denied**:
   ```bash
   flatpak install --user moonlight-steamdeck-*.flatpak
   ```

3. **Steam integration issues**:
   - Ensure you're in Desktop Mode
   - Check that the flatpak command path is correct
   - Verify Steam has permission to run flatpak

## Development

### Local Development

```bash
# Development build (clean + build)
make dev

# Production build
make prod

# Test installation
make test
```

### Adding Features

1. Modify `moonlight-steamdeck.yml` for new dependencies
2. Update the build script if needed
3. Test with `make test`
4. Create a pull request

### Release Process

1. Update version in code
2. Create and push a tag:
   ```bash
   make release
   ```
3. GitHub Actions will automatically create a release

## Support

- **Moonlight Project**: https://github.com/moonlight-stream/moonlight-qt
- **Steam Deck Support**: https://help.steampowered.com/
- **Flatpak Documentation**: https://docs.flatpak.org/

## License

This build system is part of the Moonlight project and follows the same license terms.