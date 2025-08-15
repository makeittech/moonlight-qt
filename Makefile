# Moonlight Steam Deck Flatpak Makefile

.PHONY: help build clean install test release

# Default target
help:
	@echo "Moonlight Steam Deck Flatpak Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  build     - Build the Steam Deck Flatpak"
	@echo "  clean     - Clean build artifacts"
	@echo "  install   - Install dependencies"
	@echo "  test      - Test the build"
	@echo "  release   - Create a new release"
	@echo "  help      - Show this help message"

# Build the Flatpak
build:
	@echo "Building Moonlight Steam Deck Flatpak..."
	./scripts/build-steamdeck-flatpak.sh

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build-steamdeck-*
	rm -rf repo-*
	rm -f moonlight-steamdeck-*.flatpak
	rm -f RELEASE_NOTES_*.md

# Install dependencies
install:
	@echo "Installing build dependencies..."
	sudo apt update
	sudo apt install -y flatpak-builder flatpak git

# Test the build
test:
	@echo "Testing Flatpak build..."
	@if [ -f moonlight-steamdeck-*.flatpak ]; then \
		echo "Flatpak build found, testing installation..."; \
		flatpak install --user moonlight-steamdeck-*.flatpak || echo "Installation test failed"; \
	else \
		echo "No Flatpak build found. Run 'make build' first."; \
	fi

# Create a new release
release:
	@echo "Creating new release..."
	@read -p "Enter version number (e.g., v6.1.1): " version; \
	git tag $$version; \
	git push origin $$version; \
	echo "Release $$version created and pushed to GitHub"

# Quick build (alias for build)
quick: build

# Development build
dev: clean build

# Production build
prod: clean build
	@echo "Production build complete!"
	@echo "Files created:"
	@ls -la moonlight-steamdeck-*.flatpak 2>/dev/null || echo "No Flatpak files found"
	@ls -la RELEASE_NOTES_*.md 2>/dev/null || echo "No release notes found"