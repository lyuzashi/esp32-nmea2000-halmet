/**
 * BME280 Temperature/Humidity/Pressure Sensor for Halmet
 * 
 * Reads BME280 sensor and sends N2K messages for:
 * - Temperature (PGN 130312)
 * - Humidity (PGN 130313)
 * - Atmospheric Pressure (PGN 130314)
 * - Environmental Parameters (PGN 130311) - combined message
 */
#include "GwHalmetBME280task.h"
#include "GwHardware.h"  // Defines _GWIIC when GWIIC_SCL is set

#ifdef BOARD_HALMET
#ifdef BME280_ENABLED
#ifdef _GWIIC

#include "GwHalmetSensor.h"
#include "GwApi.h"
#include "GwLog.h"
#include "GWConfig.h"
#include "N2kMessages.h"
#include <Adafruit_BME280.h>

class BME280Sensor : public HalmetSensor {
public:
    Adafruit_BME280 *device = nullptr;
    TwoWire *cachedWire = nullptr;  // Cache for reinit
    int iid = 99;  // N2K instance ID
    tN2kTempSource tempSource = N2kts_OutsideTemperature;
    tN2kHumiditySource humidSource = N2khs_OutsideHumidity;
    tN2kPressureSource pressSource = N2kps_Atmospheric;
    float tempOffset = 0;  // Temperature calibration offset in °C
    bool hasHumiditySensor = false;  // Cached: true for BME280, false for BMP280
    int consecutiveErrors = 0;  // Track errors for auto-recovery
    static const int MAX_ERRORS_BEFORE_REINIT = 3;
    
    // Sanity check bounds for readings
    static constexpr float MIN_TEMP_C = -40.0f;
    static constexpr float MAX_TEMP_C = 85.0f;
    static constexpr float MIN_PRESSURE_PA = 30000.0f;   // ~300 hPa (high altitude)
    static constexpr float MAX_PRESSURE_PA = 110000.0f;  // ~1100 hPa (extreme low)
    
    BME280Sensor() : HalmetSensor("BME280") {
        busId = 1;
        addr = 0x76;  // Default BME280 address (can also be 0x77)
        intervalMs = 10000;  // 10 seconds default
    }
    
    bool initDevice(GwApi *api, TwoWire *wire) {
        GwLog *logger = api->getLogger();
        
        if (device) {
            delete device;
            device = nullptr;
        }
        
        device = new Adafruit_BME280();
        if (!device) {
            LOG_DEBUG(GwLog::ERROR, "BME280: failed to allocate");
            return false;
        }
        
        if (!device->begin(addr, wire)) {
            LOG_DEBUG(GwLog::ERROR, "BME280: init failed at 0x%02X", addr);
            delete device;
            device = nullptr;
            return false;
        }
        
        // Apply temperature offset if configured
        if (tempOffset != 0) {
            device->setTemperatureCompensation(tempOffset);
        }
        
        // Check sensor ID: 0x60 = BME280 (has humidity), 0x58 = BMP280 (no humidity)
        uint32_t sensorId = device->sensorID();
        hasHumiditySensor = (sensorId == 0x60);
        
        consecutiveErrors = 0;
        return true;
    }
    
    bool init(GwApi *api, TwoWire *wire) override {
        GwLog *logger = api->getLogger();
        cachedWire = wire;  // Save for potential reinit
        
        if (!initDevice(api, wire)) {
            return false;
        }
        
        LOG_DEBUG(GwLog::LOG, "BME280: ready at 0x%02X, sensorID=0x%02X%s", 
                  addr, device->sensorID(), hasHumiditySensor ? "" : " (no humidity)");
        
        return true;
    }
    
    bool isReadingValid(float tempC, float pressurePa) {
        // Check for NaN
        if (isnan(tempC) || isnan(pressurePa)) return false;
        
        // Sanity check temperature
        if (tempC < MIN_TEMP_C || tempC > MAX_TEMP_C) return false;
        
        // Sanity check pressure
        if (pressurePa < MIN_PRESSURE_PA || pressurePa > MAX_PRESSURE_PA) return false;
        
        return true;
    }
    
    void measure(GwApi *api, TwoWire *wire, int counterId) override {
        if (!device) return;
        
        GwLog *logger = api->getLogger();
        
        float tempC = device->readTemperature();
        float pressurePa = device->readPressure();
        float humidity = hasHumiditySensor ? device->readHumidity() : 0;
        
        // Check for read errors or out-of-range values
        if (!isReadingValid(tempC, pressurePa)) {
            consecutiveErrors++;
            LOG_DEBUG(GwLog::DEBUG, "BME280: invalid reading (errors=%d) temp=%.1f press=%.0f", 
                      consecutiveErrors, tempC, pressurePa);
            
            // After several consecutive errors, try to reinitialize
            if (consecutiveErrors >= MAX_ERRORS_BEFORE_REINIT) {
                LOG_DEBUG(GwLog::LOG, "BME280: too many errors, reinitializing sensor");
                if (initDevice(api, cachedWire)) {
                    LOG_DEBUG(GwLog::LOG, "BME280: reinit successful");
                    api->increment(counterId, "reinit");
                } else {
                    LOG_DEBUG(GwLog::ERROR, "BME280: reinit failed");
                }
            }
            return;
        }
        
        // Good reading - reset error counter
        consecutiveErrors = 0;
        
        double tempK = CToKelvin(tempC);
        bool validHumidity = hasHumiditySensor && !isnan(humidity) && humidity > 0;
        
        LOG_DEBUG(GwLog::DEBUG, "BME280: %.1f°C, %.0f%%, %.0fPa", 
                  tempC, validHumidity ? humidity : 0, pressurePa);
        
        tN2kMsg msg;
        
        // Send temperature (PGN 130312)
        SetN2kTemperatureExt(msg, 1, iid, tempSource, tempK, N2kDoubleNA);
        api->sendN2kMessage(msg);
        api->increment(counterId, "temp");
        
        // Send humidity (PGN 130313) - only if BME280 (not BMP280)
        if (validHumidity) {
            SetN2kHumidity(msg, 1, iid, humidSource, humidity);
            api->sendN2kMessage(msg);
            api->increment(counterId, "humid");
        }
        
        // Send pressure (PGN 130314) - value is in Pa
        SetN2kPressure(msg, 1, iid, pressSource, pressurePa);
        api->sendN2kMessage(msg);
        api->increment(counterId, "press");
        
        // Send combined environmental parameters (PGN 130311)
        // This message combines temp, humidity, and pressure
        SetN2kEnvironmentalParameters(msg, 1, tempSource, tempK, 
                                       humidSource, validHumidity ? humidity : N2kDoubleNA, 
                                       pressurePa);
        api->sendN2kMessage(msg);
    }
};

// Register the sensor
void bme280TaskInit(GwApi *api) {
    GwLog *logger = api->getLogger();
    GwConfigHandler *config = api->getConfig();
    
    LOG_DEBUG(GwLog::LOG, "BME280: registering");
    
    auto sensor = std::make_shared<BME280Sensor>();
    
    // Read configuration
    sensor->iid = config->getInt("BME280iid", 99);
    sensor->intervalMs = config->getInt("BME280intv", 10) * 1000;
    sensor->tempOffset = config->getInt("BME280toff", 0);
    
    LOG_DEBUG(GwLog::LOG, "BME280: iid=%d, interval=%dms, tempOffset=%.1f", 
              sensor->iid, (int)sensor->intervalMs, sensor->tempOffset);
    
    // Register with I2C task via update()
    api->taskInterfaces()->update<ConfiguredHalmetSensors>([sensor](ConfiguredHalmetSensors *list) {
        list->sensors.push_back(sensor);
        return true;
    });
    
    LOG_DEBUG(GwLog::LOG, "BME280: registered");
}

#endif  // _GWIIC
#endif  // BME280_ENABLED
#endif  // BOARD_HALMET
