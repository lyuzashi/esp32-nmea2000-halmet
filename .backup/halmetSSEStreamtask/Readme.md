# SSE Stream Task

Streams NMEA2000 messages via Server-Sent Events (SSE) over HTTP for the Halmet board.

## Features

- Real-time streaming of N2K messages over HTTP SSE
- Messages formatted in NavLink Blue format (!PDGY,...)
- Auto-enables when clients connect, disables when all disconnect (saves CPU)
- Compatible with browser EventSource API

## Endpoint

`GET /api/stream/nmea`

## Message Format

Messages are sent as SSE events with:
- Event type: `nmea`
- Data format: `!PDGY,<pgn>,<priority>,<source>,<destination>,<timer>,<base64data>`

Example:
```
event: nmea
data: !PDGY,127250,2,22,255,12345.678,AAABBCC==
```

## Usage

### Enable in build

Add to your platformio.ini build_flags:
```ini
-D SSE_STREAM_ENABLED
```

### Connect from JavaScript

```javascript
const evtSource = new EventSource('/api/stream/nmea');

evtSource.addEventListener('nmea', (event) => {
  console.log('NMEA:', event.data);
  // Parse: !PDGY,pgn,priority,source,dest,timer,base64data
});

evtSource.addEventListener('status', (event) => {
  console.log('Status:', event.data);
});

evtSource.onerror = (error) => {
  console.error('SSE error:', error);
};
```

### Connect from curl

```bash
curl -N http://192.168.4.1/api/stream/nmea
```

## Status Display

Counter shown in web interface: `SSEStream: <clients>c/<messages>m`

## Channel Configuration

- Channel ID: 251
- Channel name: "SSE"
- Write-only (messages flow out to SSE clients)
- Uses Actisense format internally

## Dependencies

- ESPAsyncWebServer (for AsyncEventSource)
- base64 encoding (from ESP32 Arduino)
