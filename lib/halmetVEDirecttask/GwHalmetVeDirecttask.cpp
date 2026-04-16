/**
 * VE.Direct Micro-Task for Halmet
 * 
 * Reads Victron MPPT/BMV data via VE.Direct protocol and sends N2K messages.
 * Non-blocking: processes available serial bytes each micro-task call.
 * 
 * Runs as a micro-task (called by halmetTask) rather than a dedicated 
 * FreeRTOS task, saving ~3KB of stack memory.
 */
#include "GwHalmetVeDirecttask.h"
#include "GwApi.h"
#include "GwHalmetTask.h"

#include "N2kMessages.h"

#include "VeDirectFrameHandler.h"
#include "VeDirectHelper.h"

#include "hardware.h"

#include "driver/uart.h"  // For uart_set_pin - allows pin remapping without reallocation

#ifdef VEDIRECT_ENABLED

// Device configuration
struct VeDirectDeviceConfig {
    int rxPin;
    uint8_t batteryInstance;
    const char* name;
};

// Configure your devices here
static const VeDirectDeviceConfig deviceConfigs[] = {
    { DIGITAL_INPUT_1, 1, "MPPT1" },
    // { DIGITAL_INPUT_2, 2, "MPPT2" },  // Uncomment when adding devices
    // { DIGITAL_INPUT_3, 3, "MPPT3" },
};
static const int NUM_DEVICES = sizeof(deviceConfigs) / sizeof(deviceConfigs[0]);

// Single shared handler - reused for each device
static VeDirectFrameHandler frameHandler;
static VeDirectHelper helper(&frameHandler);

static HardwareSerial* g_serialVE = nullptr;

// State (NO api storage - passed to micro-task)
static int g_currentDevice = 0;
static unsigned long g_lastSwitchTime = 0;
static unsigned long g_lastFrameTime = 0;
static const unsigned long DEVICE_TIMEOUT_MS = 5000;  // Switch device if no frame for 5s
static const unsigned long MIN_DEVICE_TIME_MS = 3000; // Stay on device at least 3s

// Send N2K messages for current data
static void sendN2kMessages(GwApi* api, GwLog* logger, uint8_t batteryInstance, const char* name) {
    double voltage = helper.getValueAsDouble("V", N2kDoubleNA) / 1000.0;
    double current = helper.getValueAsDouble("I", N2kDoubleNA) / 1000.0;
    
    if (!N2kIsNA(voltage)) {
        tN2kMsg batteryMsg;
        SetN2kDCBatStatus(batteryMsg, batteryInstance, voltage, current);
        api->sendN2kMessage(batteryMsg);
        
        logger->logDebug(GwLog::DEBUG, "VE.Direct %s: %.2fV %.2fA", name, voltage, current);
    }
    
    tN2kMsg dcStatusMsg;
    SetN2kDCStatus(dcStatusMsg, 0xff, batteryInstance, tN2kDCType::N2kDCt_Battery, 
                  N2kUInt8NA, N2kUInt8NA, N2kDoubleNA, N2kDoubleNA);
    api->sendN2kMessage(dcStatusMsg);
}

static void switchToDevice(GwLog* logger, int deviceIdx) {
    const VeDirectDeviceConfig& cfg = deviceConfigs[deviceIdx];
    
    if (!g_serialVE) {
        // First time: initialize Serial
        g_serialVE = new HardwareSerial(2);
        g_serialVE->begin(19200, SERIAL_8N1, cfg.rxPin, -1);
    } else if (NUM_DEVICES > 1) {
        // Remap RX pin without buffer reallocation
        uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, cfg.rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        
        // Flush leftover data
        while (g_serialVE->available()) {
            g_serialVE->read();
        }
    }
    
    // Clear old frame data
    frameHandler.veEnd = 0;
    frameHandler.veData[0].veName[0] = '\0';
    
    g_currentDevice = deviceIdx;
    g_lastSwitchTime = millis();
    
    logger->logDebug(GwLog::DEBUG, "VE.Direct: now reading %s (GPIO %d)", cfg.name, cfg.rxPin);
}

static bool initHardware(GwApi* api) {
    GwLog* logger = api->getLogger();
    logger->logDebug(GwLog::LOG, "VE.Direct: %d device(s) configured", NUM_DEVICES);
    
    switchToDevice(logger, 0);
    g_lastFrameTime = millis();
    
    return true;
}

void veDirectInit(GwApi *api)
{
    api->getLogger()->logDebug(GwLog::LOG, "VE.Direct: will init on first micro-task call");
    
    // Register micro-task with init callback
    halmetRegisterMicroTask("VEDirect", 
        // Periodic task
        [](GwApi* api) {
            GwLog* logger = api->getLogger();
            
            if (!g_serialVE) return;
            
            const VeDirectDeviceConfig& cfg = deviceConfigs[g_currentDevice];
            unsigned long now = millis();
            
            // Read all available serial data (non-blocking)
            while (g_serialVE->available()) {
                frameHandler.rxData(g_serialVE->read());
            }
            
            // Check if we got a complete frame
            if (frameHandler.veEnd > 0) {
                sendN2kMessages(api, logger, cfg.batteryInstance, cfg.name);
                g_lastFrameTime = now;
                
                // Clear frame for next read
                frameHandler.veEnd = 0;
                frameHandler.veData[0].veName[0] = '\0';
                
                // For multi-device: rotate after getting a frame (if been on device long enough)
                if (NUM_DEVICES > 1 && (now - g_lastSwitchTime) >= MIN_DEVICE_TIME_MS) {
                    int next = (g_currentDevice + 1) % NUM_DEVICES;
                    switchToDevice(logger, next);
                }
            } else {
                // No frame yet - check for timeout (device not responding)
                if ((now - g_lastFrameTime) > DEVICE_TIMEOUT_MS && NUM_DEVICES > 1) {
                    logger->logDebug(GwLog::DEBUG, "VE.Direct %s: timeout, switching", cfg.name);
                    int next = (g_currentDevice + 1) % NUM_DEVICES;
                    switchToDevice(logger, next);
                    g_lastFrameTime = now;  // Reset timeout for new device
                }
            }
        },
        // One-time init callback
        initHardware
    );
}

#endif