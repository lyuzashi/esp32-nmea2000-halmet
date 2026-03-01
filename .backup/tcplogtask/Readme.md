# TCP Log Task

A standalone task that streams ESP32 logs over TCP, compatible with the Microsoft Serial Monitor VSCode extension.

## Features

- **Zero modifications to main code** - integrates via the user task system
- **Automatic initialization** - hooks into system startup
- **Client auto-reconnect** - handles disconnections gracefully
- **WiFi-aware** - starts server when WiFi connects
- **Low overhead** - only checks connections periodically

## How It Works

This task:
1. Creates a TCP server on port 8880 (configurable)
2. Installs itself as the system log writer via `GwApi`
3. Runs as a user task to handle client connections
4. Streams all log output to connected TCP clients

## Installation

### 1. Enable the task

Add to your build configuration (e.g., in `platformio.ini` or a user task config):

```ini
[env:halmet]
build_flags = 
    -D TCPLOGTASK=1
```

### 2. Register the task

The task auto-registers via the user task system. Just ensure it's compiled and linked.

### 3. Build and upload

```bash
platformio run -e halmet -t upload
```

## Usage

### Connect with Microsoft Serial Monitor

1. Build and upload your firmware
2. Wait for ESP32 to connect to WiFi
3. Note the IP address from the serial console
4. In VSCode:
   - Open Command Palette (Cmd/Ctrl+Shift+P)
   - Select "Serial Monitor: Focus on Serial Monitor View"
   - Click "+" to add connection
   - Enter: `tcp://192.168.1.95:8880` (use your ESP32's IP)
   - Click Connect

### Alternative Tools

The TCP stream also works with:
- **PuTTY**: Set to Raw mode, enter IP:port
- **netcat**: `nc 192.168.1.95 8880`
- **telnet**: `telnet 192.168.1.95 8880`
- Any TCP socket client

## Configuration

### Change Port

Modify `TCP_LOG_PORT` in `GwTcpLogTask.cpp`:

```cpp
#define TCP_LOG_PORT 8880  // Change to your preferred port
```

Or set via build flag:
```ini
build_flags = 
    -D TCP_LOG_PORT=2323
```

### Disable Task

Remove or comment out the build flag:
```ini
; -D TCPLOGTASK=1
```

## Status Detection

The task adds a capability that can be queried via the API:

```json
{
  "capabilities": {
    "tcpLog": "8880"
  }
}
```

## Troubleshooting

### "Server not starting"
- Ensure WiFi is connected before TCP server starts
- Check that port is not already in use
- Verify firewall settings

### "Connection refused"
- Confirm correct IP address (check status API or serial console)
- Try ping to verify network connectivity
- Check that ESP32's WiFi is stable

### "No log output"
- Verify log level is set appropriately
- Check that task is enabled in build
- Ensure client is actually connected

## Technical Details

### Implementation
- Inherits from `GwLogWriter` for log integration
- Uses `GwUserTask` for connection management
- Single client support (auto-replaces on new connection)
- Non-blocking operations

### Performance
- Connection check: Every 1 second
- Write overhead: Minimal (only when client connected)
- Memory: ~200 bytes (server) + ~2KB per client

### Port Recommendations
- **8880** (default): Non-standard, unlikely conflict
- **2323**: Common Telnet alternative
- **10111+**: High ports, minimal conflicts

Avoid: 80 (HTTP), 10110 (NMEA), 443 (HTTPS), 8080 (alt HTTP)

## Integration Example

```cpp
// The task registers itself, no manual code needed!
// Just build with -D TCPLOGTASK=1

// Optional: Check status in your code
#ifdef TCPLOGTASK
extern TcpLogWriter *tcpLogWriter;
if (tcpLogWriter && tcpLogWriter->isClientConnected()) {
    // Client is connected
}
#endif
```

## License

Same as parent project.
