# NMEA2000 Message Logger Task

A standalone task that logs all raw binary NMEA2000 messages flowing through the system without modifying core code.

## What it does

- Receives binary NMEA2000 messages via Actisense format
- Logs complete message details: PGN, priority, source, destination, data length
- Displays raw message data in hexadecimal format (8 bytes per line)
- Tracks message count
- Provides complete visibility into all N2K traffic

## How it works

1. **N2kLoggerStream**: Custom Stream implementation that:
   - Receives Actisense-formatted N2K messages from the main loop
   - Parses the Actisense protocol using tActisenseReader
   - Logs each message with full binary details

2. **LoggerChannelImpl**: Channel implementation that:
   - Provides the N2kLoggerStream via `getStream()`
   - Registers with writeActisense enabled to receive N2K messages

3. **loggerInit()**: Initialization function that:
   - Creates and configures the logger channel
   - Automatically registers via `DECLARE_INITFUNCTION`
   - No modifications to main.cpp required

## Files

- `GwLoggerTask.h` - Header with init function declaration
- `GwLoggerTask.cpp` - Complete implementation
- `Readme.md` - This file

## Usage

Simply include this library in your build. The task automatically registers itself and starts logging all NMEA2000 messages that flow through `handleN2kMessage()`.

**No code modifications required** - the channel receives messages through the existing distribution mechanism in the main loop.

## Output Example

```
[N2kLogger] RX #1: PGN=127250 Pri=2 Src=1 Dst=255 Len=8
  Data: 01 FF 7F 88 13 00 00 FF
[N2kLogger] RX #2: PGN=129029 Pri=3 Src=1 Dst=255 Len=43
  Data: FF 7F 4D 3E 95 01 00 00
        00 00 C0 66 40 66 91 3C
        BB 3C 11 01 42 80 00 F0
        04 2E 83 64 FD 10 00 00
        00 00 00 FF 7F 7A 14 FF
        7F FF 7F
[N2kLogger] RX #3: PGN=130306 Pri=3 Src=1 Dst=255 Len=8
  Data: 32 7F 00 00 00 FF 7F 1C
```

## Notes

- All messages passing through the system are logged
- Includes messages from CAN bus and messages being sent to CAN bus
- Message data format is hexadecimal for easy analysis
- RX count tracks total messages received by the logger
- The Actisense format is automatically parsed by the ActisenseReader library
