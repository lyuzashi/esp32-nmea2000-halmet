/**
 * 1-Wire Temperature Sensor Task for Halmet
 * 
 * Reads DS18B20 (and compatible) temperature sensors on 1-Wire bus.
 * Maps configured device addresses to N2K temperature source types.
 */
#include "GwHalmet1WireTask.h"

#ifdef BOARD_HALMET
#ifdef ONEWIRE_ENABLED

#include "hardware.h"

#include "GwLog.h"
#include "GWConfig.h"
#include "N2kMessages.h"

#include <OneWire.h>
#include <DallasTemperature.h>

// Initialized lazily in task to avoid static init issues
static OneWire* oneWire = nullptr;
static DallasTemperature* sensors = nullptr;

// Maximum configured sensors
#define MAX_CONFIGURED_SENSORS 4

// Sensor types
enum SensorType {
    SENSOR_TYPE_TEMPERATURE,    // Uses SetN2kTemperatureExt
    SENSOR_TYPE_ENGINE_OIL      // Uses SetN2kEngineDynamicParam for oil temp
};

// Sensor configuration structure
struct SensorConfig {
    DeviceAddress address;
    SensorType type;
    tN2kTempSource tempSource;  // Only used for SENSOR_TYPE_TEMPERATURE
    uint8_t instance;
    bool configured;
    const char* name;  // Static string, no heap allocation
};

// Convert device address to hex string (no heap allocation)
static void addressToHex(const DeviceAddress addr, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i*2] = hex[addr[i] >> 4];
        out[i*2 + 1] = hex[addr[i] & 0x0F];
    }
    out[16] = '\0';
}

// Parse hex string to device address using sscanf
// Returns true if valid 16-char hex string was parsed
static bool hexToAddress(const char* str, DeviceAddress addr) {
    if (strlen(str) != 16) return false;
    
    unsigned int bytes[8];
    int parsed = sscanf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
                        &bytes[0], &bytes[1], &bytes[2], &bytes[3],
                        &bytes[4], &bytes[5], &bytes[6], &bytes[7]);
    if (parsed != 8) return false;
    
    for (int i = 0; i < 8; i++) {
        addr[i] = (uint8_t)bytes[i];
    }
    return true;
}

// Check if two addresses match
static bool addressesMatch(const DeviceAddress a, const DeviceAddress b) {
    return memcmp(a, b, 8) == 0;
}

static void oneWireTask(GwApi *api)
{
    GwLog* logger = api->getLogger();
    GwConfigHandler* config = api->getConfig();
    
    // Small delay to let system stabilize before 1-Wire init
    delay(100);
    
    LOG_DEBUG(GwLog::LOG, "1-Wire: initializing on GPIO %d", ONEWIRE_PIN);
    
    // Initialize 1-Wire bus (lazy init to avoid static constructor issues)
    oneWire = new OneWire(ONEWIRE_PIN);
    if (!oneWire) {
        LOG_DEBUG(GwLog::ERROR, "1-Wire: failed to allocate OneWire");
        vTaskDelete(NULL);
        return;
    }
    sensors = new DallasTemperature(oneWire);
    if (!sensors) {
        LOG_DEBUG(GwLog::ERROR, "1-Wire: failed to allocate DallasTemperature");
        delete oneWire;
        vTaskDelete(NULL);
        return;
    }
    
    // Load configured sensor addresses
    SensorConfig sensorConfigs[MAX_CONFIGURED_SENSORS] = {
        { {0}, SENSOR_TYPE_TEMPERATURE, N2kts_SeaTemperature, 1, false, "SeaTemp" },
        { {0}, SENSOR_TYPE_TEMPERATURE, N2kts_EngineRoomTemperature, 1, false, "EngineRoom" },
        { {0}, SENSOR_TYPE_ENGINE_OIL, N2kts_SeaTemperature, 1, false, "EngineOil" },  // Uses PGN 127489
        { {0}, SENSOR_TYPE_TEMPERATURE, N2kts_ExhaustGasTemperature, 1, false, "ExhaustGas" }
    };
    
    // Config item names (must match config.json)
    const char* configNames[MAX_CONFIGURED_SENSORS] = {
        "owSeaTempAddr",
        "owEngRoomAddr", 
        "owEngineOilAddr",
        "owExhGasAddr"
    };
    
    // Temporary buffer for address strings (avoids heap allocation)
    char addrBuf[17];
    
    int configuredCount = 0;
    for (int i = 0; i < MAX_CONFIGURED_SENSORS; i++) {
        String addrStr;
        if (config->getValue(addrStr, configNames[i])) {
            addrStr.toLowerCase();
            addrStr.trim();
            if (addrStr.length() > 0) {
                if (hexToAddress(addrStr.c_str(), sensorConfigs[i].address)) {
                    sensorConfigs[i].configured = true;
                    configuredCount++;
                    LOG_DEBUG(GwLog::LOG, "1-Wire: %s configured as %s", 
                              sensorConfigs[i].name, addrStr.c_str());
                } else {
                    LOG_DEBUG(GwLog::LOG, "1-Wire: invalid address for %s: %s", 
                              sensorConfigs[i].name, addrStr.c_str());
                }
            }
        }
    }
    
    LOG_DEBUG(GwLog::LOG, "1-Wire: %d sensor(s) configured", configuredCount);
    
    sensors->begin();
    
    // Small delay after begin() for bus to stabilize
    delay(50);
    
    // Scan for devices
    int deviceCount = sensors->getDeviceCount();
    LOG_DEBUG(GwLog::LOG, "1-Wire: found %d device(s) on bus", deviceCount);
    
    // Add counter group for status display - discovered addresses show in expanded view
    int counterId = api->addCounter("1wire");
    
    // Log all discovered device addresses
    DeviceAddress addr;
    for (int i = 0; i < deviceCount; i++) {
        if (sensors->getAddress(addr, i)) {
            addressToHex(addr, addrBuf);
            
            // Check if this device matches any configured sensor
            bool matched = false;
            for (int j = 0; j < MAX_CONFIGURED_SENSORS; j++) {
                if (sensorConfigs[j].configured && addressesMatch(addr, sensorConfigs[j].address)) {
                    LOG_DEBUG(GwLog::LOG, "1-Wire device %d: %s -> %s", 
                              i, addrBuf, sensorConfigs[j].name);
                    // Show configured sensors by name in counter display
                    api->increment(counterId, sensorConfigs[j].name);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                LOG_DEBUG(GwLog::LOG, "1-Wire device %d: %s (unconfigured)", i, addrBuf);
                // Show unconfigured addresses in the counter display so user can copy them
                api->increment(counterId, addrBuf);
            }
        }
    }
    
    if (deviceCount == 0) {
        api->increment(counterId, "no_devices");
        LOG_DEBUG(GwLog::LOG, "1-Wire: no devices found, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    // Set resolution (9-12 bits, higher = slower but more precise)
    sensors->setResolution(12);
    
    // Use async mode for non-blocking reads
    sensors->setWaitForConversion(false);
    
    while (true) {
        // Request temperatures from all devices
        sensors->requestTemperatures();
        
        // Wait for conversion (750ms for 12-bit resolution)
        delay(750);
        
        // Read each configured sensor and send N2K messages
        for (int i = 0; i < MAX_CONFIGURED_SENSORS; i++) {
            if (!sensorConfigs[i].configured) continue;
            
            float tempC = sensors->getTempC(sensorConfigs[i].address);
            
            if (tempC == DEVICE_DISCONNECTED_C) {
                LOG_DEBUG(GwLog::DEBUG, "1-Wire %s: disconnected", sensorConfigs[i].name);
                continue;
            }
            
            LOG_DEBUG(GwLog::DEBUG, "1-Wire %s: %.2f°C", sensorConfigs[i].name, tempC);
            
            // Increment counter for this sensor (shows reading count in status)
            // Use const char* directly to avoid String allocation
            api->increment(counterId, sensorConfigs[i].name);
            
            // Send appropriate N2K message based on sensor type
            tN2kMsg msg;
            double tempK = CToKelvin(tempC);
            
            if (sensorConfigs[i].type == SENSOR_TYPE_ENGINE_OIL) {
                // Use PGN 127489 (Engine Parameters Dynamic) for oil temperature
                // Must provide Status1/Status2 to resolve overload ambiguity
                tN2kEngineDiscreteStatus1 status1 = {0};
                tN2kEngineDiscreteStatus2 status2 = {0};
                SetN2kEngineDynamicParam(msg, sensorConfigs[i].instance,
                                         N2kDoubleNA,  // EngineOilPress
                                         tempK,        // EngineOilTemp
                                         N2kDoubleNA,  // EngineCoolantTemp
                                         N2kDoubleNA,  // AlternatorVoltage
                                         N2kDoubleNA,  // FuelRate
                                         N2kDoubleNA,  // EngineHours
                                         N2kDoubleNA,  // EngineCoolantPress
                                         N2kDoubleNA,  // EngineFuelPress
                                         N2kInt8NA,    // EngineLoad
                                         N2kInt8NA,    // EngineTorque
                                         status1, status2);
            } else {
                // Use SetN2kTemperatureExt for other temperature types
                SetN2kTemperatureExt(msg, 1, sensorConfigs[i].instance, 
                                     sensorConfigs[i].tempSource, tempK, N2kDoubleNA);
            }
            api->sendN2kMessage(msg);
        }
        
        // Read every 5 seconds
        delay(4250);  // 750ms conversion + 4250ms = 5000ms total
    }
    
    vTaskDelete(NULL);
}

void oneWireInit(GwApi *api)
{
    api->addUserTask(oneWireTask, "oneWireTask", 4096);
}

#endif  // ONEWIRE_ENABLED
#endif  // BOARD_HALMET
