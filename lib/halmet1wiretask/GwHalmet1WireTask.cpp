/**
 * 1-Wire Temperature Sensor Micro-Task for Halmet
 * 
 * Reads DS18B20 (and compatible) temperature sensors on 1-Wire bus.
 * Maps configured device addresses to N2K temperature source types.
 * 
 * Runs as a micro-task (called every ~5s by halmetTask) rather than a 
 * dedicated FreeRTOS task, saving ~4KB of stack memory.
 * 
 * Hardware init is deferred to first micro-task call (not during init phase).
 */
#include "GwHalmet1WireTask.h"

#ifdef BOARD_HALMET
#ifdef ONEWIRE_ENABLED

#include "hardware.h"

#include "GwLog.h"
#include "GWConfig.h"
#include "GwHalmetTask.h"
#include "N2kMessages.h"

#include <OneWire.h>
#include <DallasTemperature.h>

// 1-Wire bus objects (created on first micro-task call)
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
    tN2kTempSource tempSource;
    uint8_t instance;
    bool configured;
    const char* name;
};

// State (NO g_api - it's passed to each micro-task call)
static int g_counterId = -1;
static SensorConfig g_sensorConfigs[MAX_CONFIGURED_SENSORS];

// Convert device address to hex string
static void addressToHex(const DeviceAddress addr, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i*2] = hex[addr[i] >> 4];
        out[i*2 + 1] = hex[addr[i] & 0x0F];
    }
    out[16] = '\0';
}

// Parse hex string to device address
static bool hexToAddress(const char* str, DeviceAddress addr) {
    if (strlen(str) != 16) return false;
    unsigned int bytes[8];
    if (sscanf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3],
               &bytes[4], &bytes[5], &bytes[6], &bytes[7]) != 8) return false;
    for (int i = 0; i < 8; i++) addr[i] = (uint8_t)bytes[i];
    return true;
}

static bool addressesMatch(const DeviceAddress a, const DeviceAddress b) {
    return memcmp(a, b, 8) == 0;
}

/**
 * Deferred hardware initialization - called on first micro-task run.
 * This matches how the original FreeRTOS task worked (init inside task).
 */
static bool initHardware(GwApi* api) {
    GwLog* logger = api->getLogger();
    GwConfigHandler* config = api->getConfig();
    
    logger->logDebug(GwLog::LOG, "1-Wire: initializing on GPIO %d", ONEWIRE_PIN);
    
    oneWire = new OneWire(ONEWIRE_PIN);
    if (!oneWire) return false;
    
    sensors = new DallasTemperature(oneWire);
    if (!sensors) {
        delete oneWire;
        oneWire = nullptr;
        return false;
    }
    
    // Initialize sensor configs
    memset(g_sensorConfigs, 0, sizeof(g_sensorConfigs));
    g_sensorConfigs[0] = {{0}, SENSOR_TYPE_TEMPERATURE, N2kts_SeaTemperature, 1, false, "SeaTemp"};
    g_sensorConfigs[1] = {{0}, SENSOR_TYPE_TEMPERATURE, N2kts_EngineRoomTemperature, 1, false, "EngineRoom"};
    g_sensorConfigs[2] = {{0}, SENSOR_TYPE_ENGINE_OIL, N2kts_SeaTemperature, 1, false, "EngineOil"};
    g_sensorConfigs[3] = {{0}, SENSOR_TYPE_TEMPERATURE, N2kts_ExhaustGasTemperature, 1, false, "ExhaustGas"};
    
    const char* configNames[MAX_CONFIGURED_SENSORS] = {
        "owSeaTempAddr", "owEngRoomAddr", "owEngineOilAddr", "owExhGasAddr"
    };
    
    char addrBuf[17];
    int configuredCount = 0;
    for (int i = 0; i < MAX_CONFIGURED_SENSORS; i++) {
        String addrStr;
        if (config->getValue(addrStr, configNames[i])) {
            addrStr.toLowerCase();
            addrStr.trim();
            if (addrStr.length() > 0 && hexToAddress(addrStr.c_str(), g_sensorConfigs[i].address)) {
                g_sensorConfigs[i].configured = true;
                configuredCount++;
                logger->logDebug(GwLog::LOG, "1-Wire: %s = %s", g_sensorConfigs[i].name, addrStr.c_str());
            }
        }
    }
    
    sensors->begin();
    
    int deviceCount = sensors->getDeviceCount();
    logger->logDebug(GwLog::LOG, "1-Wire: found %d device(s)", deviceCount);
    
    g_counterId = api->addCounter("1wire");
    
    DeviceAddress addr;
    for (int i = 0; i < deviceCount; i++) {
        if (sensors->getAddress(addr, i)) {
            addressToHex(addr, addrBuf);
            bool matched = false;
            for (int j = 0; j < MAX_CONFIGURED_SENSORS; j++) {
                if (g_sensorConfigs[j].configured && addressesMatch(addr, g_sensorConfigs[j].address)) {
                    api->increment(g_counterId, g_sensorConfigs[j].name);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                api->increment(g_counterId, addrBuf);
            }
        }
    }
    
    if (deviceCount == 0) {
        api->increment(g_counterId, "no_devices");
        return false;
    }
    
    sensors->setResolution(12);
    sensors->setWaitForConversion(false);
    sensors->requestTemperatures();
    
    return true;
}

void oneWireInit(GwApi *api)
{
    // Just register micro-task - NO api storage (it gets deleted after init!)
    api->getLogger()->logDebug(GwLog::LOG, "1-Wire: will init on first micro-task call");
    
    // Register micro-task with init callback
    halmetRegisterMicroTask("1Wire", 
        // Periodic task
        [](GwApi* api) {
            if (!sensors) return;
            
            // Read temperatures and send N2K messages
            for (int i = 0; i < MAX_CONFIGURED_SENSORS; i++) {
                if (!g_sensorConfigs[i].configured) continue;
                
                float tempC = sensors->getTempC(g_sensorConfigs[i].address);
                if (tempC == DEVICE_DISCONNECTED_C) continue;
                
                if (g_counterId >= 0) {
                    api->increment(g_counterId, g_sensorConfigs[i].name);
                }
                
                tN2kMsg msg;
                double tempK = CToKelvin(tempC);
                
                if (g_sensorConfigs[i].type == SENSOR_TYPE_ENGINE_OIL) {
                    tN2kEngineDiscreteStatus1 status1 = {0};
                    tN2kEngineDiscreteStatus2 status2 = {0};
                    SetN2kEngineDynamicParam(msg, g_sensorConfigs[i].instance,
                                             N2kDoubleNA, tempK, N2kDoubleNA, N2kDoubleNA,
                                             N2kDoubleNA, N2kDoubleNA, N2kDoubleNA, N2kDoubleNA,
                                             N2kInt8NA, N2kInt8NA, status1, status2);
                } else {
                    SetN2kTemperatureExt(msg, 1, g_sensorConfigs[i].instance, 
                                         g_sensorConfigs[i].tempSource, tempK, N2kDoubleNA);
                }
                api->sendN2kMessage(msg);
            }
            
            sensors->requestTemperatures();
        },
        // One-time init callback
        initHardware
    );
}

#endif  // ONEWIRE_ENABLED
#endif  // BOARD_HALMET
