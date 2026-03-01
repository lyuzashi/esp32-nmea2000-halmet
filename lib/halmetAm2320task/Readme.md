# AM2320 Temperature and Humidity Sensor Task

This task provides support for the AM2320 I2C temperature and humidity sensor.

## Features

- **Temperature Measurement**: Measures temperature from -40°C to +80°C (±0.5°C accuracy)
- **Humidity Measurement**: Measures relative humidity from 0-100% (±3% accuracy)
- **I2C Interface**: Default address 0x5C
- **NMEA2000 Output**: Sends PGN 130312 (Temperature) and PGN 130313 (Humidity)
- **NMEA0183 XDR**: Configurable transducer names for XDR conversion
- **Calibration**: Temperature and humidity offset adjustment

## Hardware Connection

The AM2320 sensor requires:
- **I2C Bus 1** (default): Connect SDA/SCL to your board's I2C pins
- **Power**: 3.3V or 5V (sensor is 5V tolerant with 3.3V logic)
- **Pull-up Resistors**: Usually 4.7kΩ on SDA and SCL (often built into sensor modules)

For the Halmet board with M5 Grove connector:
- Red: 5V
- Black: GND
- White: SCL (GPIO 22)
- Yellow: SDA (GPIO 21)

## Configuration

All settings are available via the web interface under "sensors":

- **Enable/Disable**: Separately control temperature and humidity readings
- **Source Types**: Set NMEA2000 source (InsideTemperature, OutsideTemperature, etc.)
- **Instance ID**: NMEA2000 instance identifier (default: 99)
- **Interval**: Measurement frequency in seconds (default: 10s)
- **XDR Names**: Custom names for NMEA0183 XDR output
- **Calibration**: Offset adjustments for temperature (°C) and humidity (%)

## NMEA2000 Messages

The task sends:
- **PGN 130312**: Temperature (extended format with SetN2kTemperatureExt)
- **PGN 130313**: Humidity (SetN2kHumidity)
- **PGN 130311**: Environmental Parameters (combined message)

Default sources:
- Temperature: InsideTemperature (2)
- Humidity: InsideHumidity (0)

## Data Flow

1. Sensor polled at configured interval
2. I2C read with CRC validation
3. Data converted to NMEA2000 format (temperature to Kelvin)
4. Messages sent to all active channels:
   - NMEA2000 bus (if connected)
   - NMEA0183 (via XDR conversion)
   - SignalK (via channel converters)
   - **NavLink BLE** (if active)

## Integration

This task automatically integrates with the existing `iicTask`:
- No separate task needed - uses shared I2C polling
- Thread-safe I2C access via centralized task
- Automatic registration with sensor framework
- Appears in status counters and data display

## Build Requirements

Add to your `platformio.ini` environment:

```ini
lib_deps = 
    adafruit/Adafruit AM2320 sensor library @ ^1.2.2

build_flags = 
    -DBOARD_HALMET
    -D_GWIIC
    -DGWIIC_SDA=21
    -DGWIIC_SCL=22
```

The task only compiles when both `BOARD_HALMET` and `_GWIIC` are defined.

**Library**: Uses the official [Adafruit AM2320 library](https://github.com/adafruit/Adafruit_AM2320) which handles all low-level I2C communication, CRC validation, and sensor quirks.

## Sensor Specifications

- **Model**: AM2320 (Aosong Electronics)
- **Interface**: I2C (400kHz max)
- **Address**: 0x5C (fixed)
- **Temperature Range**: -40°C to +80°C
- **Temperature Accuracy**: ±0.5°C
- **Humidity Range**: 0-100% RH
- **Humidity Accuracy**: ±3% RH (at 25°C)
- **Response Time**: < 2 seconds
- **Power**: 3.1-5.5V, < 1mA during measurement

## Troubleshooting

**Sensor not detected**:
- Check I2C wiring (SDA/SCL not swapped)
- Verify 3.3V/5V power
- Ensure pull-up resistors present (4.7kΩ typical)
- Check logs for initialization errors

**CRC errors**:
- Cable too long (keep under 1 meter for reliable I2C)
- EMI interference (route away from power lines)
- Bad connection or loose wires

**Readings seem wrong**:
- Use calibration offsets to adjust
- Allow sensor to stabilize (2-3 minutes after power-on)
- Check mounting location (avoid direct sunlight, heat sources)
