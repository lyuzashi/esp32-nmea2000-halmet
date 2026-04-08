# WebSocket Stream Task

Streams NMEA2000 messages via WebSocket for the Halmet board.

## Features

- Real-time streaming of N2K messages over WebSocket
- Messages formatted in NavLink Blue format (!PDGY,...)
- Auto-enables when clients connect, disables when all disconnect (saves CPU)
- Compatible with browser WebSocket API

## Endpoint

`ws://<device-ip>/ws`

## Message Format

Messages are sent as text frames:
`!PDGY,<pgn>,<priority>,<source>,<destination>,<timer>,<base64data>`

Example:
```
!PDGY,127250,2,22,255,12345.678,AAABBCC==
```

## Usage

### Enable in build

Add to your platformio.ini build_flags:
```ini
-D WS_STREAM_ENABLED
```

### Connect from JavaScript

```javascript
const ws = new WebSocket('ws://192.168.4.1/ws');

ws.onopen = () => console.log('Connected');

ws.onmessage = (event) => {
  if (event.data.startsWith('!PDGY')) {
    // Parse: !PDGY,pgn,priority,source,dest,timer,base64data
    const parts = event.data.split(',');
    console.log('PGN:', parts[1], 'Data:', parts[6]);
  }
};

ws.onerror = (error) => {
  console.error('WebSocket error:', error);
};
```

### Connect from command line

```bash
websocat ws://192.168.4.1/ws
```

## Status Display

Counter shown in web interface: `WSStream: <clients>c/<messages>m`

## Channel Configuration

- Channel ID: 251
- Channel name: "WS"
- Write-only (messages flow out to WebSocket clients)
- Uses Actisense format internally

## Dependencies

- ESPAsyncWebServer (for AsyncWebSocket)
- base64 encoding (from ESP32 Arduino)
