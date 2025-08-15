# Moonlight Auto-Connect to Last Host Feature

This implementation adds automatic connection functionality to Moonlight that will attempt to connect to the last used host when the application launches.

## Features

### Automatic Connection on Launch
- Automatically connects to the last host used for streaming when Moonlight starts
- Only attempts connection if the host is online and paired
- Configurable setting to enable/disable the feature
- Silent failure - if auto-connection fails, the normal UI is shown

### Last Host Tracking
- Automatically tracks which host was last used for streaming
- Stores the host UUID in persistent settings
- Updates the last used host whenever a new streaming session is started

### Smart Connection Logic
- Only attempts connection if the host is online (CS_ONLINE)
- Only attempts connection if the host is paired (PS_PAIRED)
- Waits 3 seconds after launch to allow host discovery
- Connects to the desktop app (app ID 0) by default

## Implementation Details

### Files Modified

1. **app/backend/computermanager.h**
   - Added auto-connection method declarations
   - Added auto-connection signals
   - Added last used host tracking member variable

2. **app/backend/computermanager.cpp**
   - Implemented `setLastUsedHost()` method
   - Implemented `getLastUsedHost()` method
   - Implemented `autoConnectToLastHost()` method
   - Added QSettings integration for persistent storage

3. **app/gui/appmodel.cpp**
   - Added tracking of last used host when creating sessions

4. **app/gui/computermodel.cpp**
   - Added tracking of last used host when creating sessions

5. **app/settings/streamingpreferences.h**
   - Added `autoConnectToLastHost` property
   - Added corresponding signal

6. **app/settings/streamingpreferences.cpp**
   - Added loading and saving of auto-connection setting
   - Default value: false (disabled by default)

7. **app/gui/main.qml**
   - Added auto-connection timer (3 second delay)
   - Added auto-connection signal handlers
   - Added logic to only auto-connect if setting is enabled

8. **app/gui/SettingsView.qml**
   - Added checkbox for enabling/disabling auto-connection
   - Added tooltip explaining the feature

### Auto-Connection Flow

1. **Application Launch**
   - Main.qml checks if auto-connection is enabled
   - If enabled, starts a 3-second timer

2. **Host Discovery Wait**
   - Timer allows time for mDNS discovery and host polling
   - Ensures hosts are available before attempting connection

3. **Auto-Connection Attempt**
   - Calls `ComputerManager.autoConnectToLastHost()`
   - Validates last used host exists and is available
   - Checks host is online and paired

4. **Connection Success**
   - Creates a session for the desktop app
   - Pushes StreamSegue to start streaming
   - User sees streaming interface immediately

5. **Connection Failure**
   - Logs error to console
   - Continues to normal UI without interruption
   - No error dialogs shown to user

### Settings Integration

The auto-connection feature is controlled by a new setting:
- **Setting Name**: "Auto-connect to last used host on launch"
- **Location**: Settings → General section
- **Default Value**: Disabled (false)
- **Storage**: QSettings with key "autoconnect"

### Error Handling

The auto-connection feature handles several failure scenarios:
- No last used host found
- Last used host not in known hosts list
- Last used host is offline
- Last used host is not paired
- Network connectivity issues

All failures are handled silently, allowing the user to continue with normal operation.

## Usage

### Enabling Auto-Connection
1. Open Moonlight settings
2. Navigate to the General section
3. Check "Auto-connect to last used host on launch"
4. Save settings

### How It Works
1. Start streaming from any host (this becomes the "last used host")
2. Close Moonlight
3. Launch Moonlight again
4. If the setting is enabled, Moonlight will automatically attempt to connect to the last used host
5. If successful, streaming begins immediately
6. If unsuccessful, the normal computer selection screen is shown

### Disabling Auto-Connection
- Uncheck the setting in preferences
- Auto-connection will be disabled for future launches

## Technical Notes

### Threading
- Auto-connection uses QTimer to avoid blocking the main thread
- Host validation is performed on the main thread
- Session creation follows existing patterns

### Persistence
- Last used host UUID is stored in QSettings
- Setting is loaded on ComputerManager initialization
- Setting is saved whenever a new host is used

### Integration
- Integrates with existing host discovery and polling
- Uses existing session creation mechanisms
- Follows existing UI navigation patterns

### Performance
- Minimal overhead - only stores a UUID string
- No impact on normal operation when disabled
- 3-second delay ensures host discovery completes

## Testing

The auto-connection feature can be tested by:
1. Enabling the setting in preferences
2. Streaming from a host (this becomes the last used host)
3. Closing Moonlight
4. Launching Moonlight again
5. Observing automatic connection to the last used host

The feature gracefully handles edge cases like:
- Host going offline between sessions
- Host becoming unpaired
- Network connectivity issues
- Multiple hosts in the list