# 🎮 Moonlight Steam Deck Flatpak

A Steam Deck optimized version of Moonlight for game streaming from your PC.

## 🚀 Quick Install

### Option 1: Download from GitHub Releases
1. Go to [Releases](https://github.com/makeittech/moonlight-qt/releases)
2. Download the latest `moonlight-steamdeck-*.flatpak`
3. Install on your Steam Deck

### Option 2: Install via Terminal
```bash
# Download and install the latest release
flatpak install https://github.com/makeittech/moonlight-qt/releases/latest/download/moonlight-steamdeck-*.flatpak
```

### Option 3: Via Discover App
1. Copy the `.flatpak` file to your Steam Deck
2. Open Discover app
3. Click "Install" on the Flatpak file

## 🎯 Steam Deck Features

- **Enhanced Controller Support**: Optimized for Steam Deck gamepads
- **Hardware Acceleration**: Full DRI access for smooth streaming
- **Steam Integration**: Works with Steam's virtual gamepad
- **Low Latency**: Optimized for gaming performance
- **4K HDR Support**: High-quality streaming capabilities

## 🎮 Add to Steam

1. Open Steam in Desktop Mode
2. Go to Games → Add a Non-Steam Game to My Library
3. Browse to `/usr/bin/flatpak`
4. Set launch options to: `run com.moonlight_stream.Moonlight`
5. Add artwork and configure as needed

## 🔧 Setup Your PC

1. **NVIDIA GeForce Experience** (NVIDIA GPUs):
   - Install GeForce Experience
   - Enable GameStream in settings
   - Add Moonlight to your PC

2. **Sunshine** (AMD/Intel GPUs):
   - Install [Sunshine](https://github.com/LizardByte/Sunshine)
   - Configure your streaming settings
   - Add Moonlight to your PC

## 📱 Connect

1. Launch Moonlight on your Steam Deck
2. Your PC should appear in the list
3. Enter the PIN shown on your PC
4. Start streaming!

## 🛠️ Troubleshooting

**Can't find your PC?**
- Ensure GeForce Experience or Sunshine is running
- Check firewall settings
- Verify both devices are on the same network

**Controller not working?**
- Try running in Desktop Mode first
- Check Steam controller settings
- Verify the Flatpak has proper permissions

**Poor performance?**
- Use a wired connection if possible
- Reduce streaming quality in Moonlight settings
- Close unnecessary applications on your PC

## 📋 System Requirements

- **Steam Deck** (or compatible Linux system)
- **Network**: Stable connection to host PC
- **Host PC**: Running GeForce Experience or Sunshine
- **Storage**: ~100MB for the Flatpak

## 🔄 Updates

The Flatpak will automatically update when you run:
```bash
flatpak update com.moonlight_stream.Moonlight
```

## 📞 Support

- **Moonlight Project**: https://github.com/moonlight-stream/moonlight-qt
- **Steam Deck Support**: https://help.steampowered.com/
- **Issues**: Report on GitHub

## 📄 License

This build is part of the Moonlight project and follows the same license terms.

---

**Enjoy streaming your games on Steam Deck! 🎮**