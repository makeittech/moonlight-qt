# Moonlight Retry Connection Feature

This implementation adds automatic retry functionality to Moonlight when the connection is lost, with an overlay display and cancel button.

## Features

### Automatic Retry Mechanism
- Automatically retries connection when network-related errors occur
- Configurable maximum retry attempts (default: 3, max: 10)
- Smart error classification to determine which errors are retryable

### Visual Overlay
- Orange overlay displayed during retry attempts
- Shows current retry count and maximum retries
- Text: "Connection lost. Retrying... (X/Y)"

### Cancel Functionality
- Press `Ctrl+Alt+C` to cancel retry attempts
- Returns to the main UI when cancelled
- Clean shutdown of retry process

## Implementation Details

### Files Modified

1. **app/streaming/session.h**
   - Added retry-related member variables
   - Added retry method declarations
   - Added retry-related signals

2. **app/streaming/session.cpp**
   - Implemented retry logic in `clConnectionTerminated()`
   - Added retry methods: `startRetry()`, `cancelRetry()`, `retryConnection()`
   - Added keyboard handling for cancel key combination
   - Integrated QTimer for non-blocking retry delays

3. **app/streaming/video/overlaymanager.h**
   - Added new overlay type: `OverlayRetry`

4. **app/streaming/video/overlaymanager.cpp**
   - Configured retry overlay with orange color and 32pt font

5. **app/gui/StreamSegue.qml**
   - Added retry signal handlers
   - Updated hint text to include cancel key combination
   - Connected retry signals to UI

### Retryable Errors

The following connection errors are considered retryable:
- `ML_ERROR_NO_VIDEO_TRAFFIC` - Network connectivity issues
- `ML_ERROR_NO_VIDEO_FRAME` - Performance/network issues
- `ML_ERROR_UNEXPECTED_EARLY_TERMINATION` - Network-related terminations
- Unknown errors with error codes < 1000 (likely network-related)

### Non-Retryable Errors

The following errors are not retryable:
- `ML_ERROR_GRACEFUL_TERMINATION` - User-initiated disconnect
- `ML_ERROR_PROTECTED_CONTENT` - Host-side DRM issues
- `ML_ERROR_FRAME_CONVERSION` - Host-side encoding issues
- Unknown errors with error codes >= 1000 (likely host-side issues)

## Usage

### During Streaming
1. If connection is lost due to network issues, the retry mechanism automatically starts
2. An orange overlay appears showing retry progress
3. The system attempts to reconnect up to the maximum number of retries

### Cancelling Retry
- Press `Ctrl+Alt+C` during retry attempts
- The retry process stops and returns to the main UI
- No further retry attempts will be made

### Configuration
- Default maximum retries: 3
- Configurable range: 0-10 retries
- Retry delays: 2 seconds between attempts, 3 seconds after failure

## Technical Notes

### Threading
- Retry logic uses QTimer to avoid blocking the main event loop
- Overlay updates are thread-safe and handled through the existing overlay system
- Connection attempts are made asynchronously

### Error Handling
- Retry mechanism only activates for network-related errors
- Host-side errors still show appropriate error dialogs
- Graceful termination (user disconnect) bypasses retry entirely

### UI Integration
- Retry overlay integrates with existing overlay system
- Cancel functionality works with existing input handling
- Error dialogs show appropriate messages for retry failures

## Testing

The retry mechanism can be tested by:
1. Starting a streaming session
2. Simulating network disconnection (e.g., disconnecting network cable, firewall blocking)
3. Observing the orange retry overlay appear
4. Testing the cancel functionality with `Ctrl+Alt+C`

The retry logic automatically handles network-related connection losses and attempts to reconnect up to the configured maximum number of attempts.