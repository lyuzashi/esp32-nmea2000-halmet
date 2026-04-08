#include "GwHalmetVeDirecttask.h"
#include "GwApi.h"

#include "N2kMessages.h"

#include "VeDirectFrameHandler.h"
#include "VeDirectHelper.h"

#include "hardware.h"

#include "freertos/task.h"
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

HardwareSerial SerialVE(2); // Use UART2

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

void veDirectTask(GwApi *api)
{
    GwLog* logger = api->getLogger();
    
    // Time to wait for a complete frame from each device
    const unsigned long frameTimeout = 2000;  // VE.Direct sends every ~1s
    
    int currentDevice = 0;
    bool serialInitialized = false;
    
    logger->logDebug(GwLog::LOG, "VE.Direct: %d device(s) configured", NUM_DEVICES);

    while (true) {
        const VeDirectDeviceConfig& cfg = deviceConfigs[currentDevice];
        
        if (!serialInitialized) {
            // First time: initialize Serial with first device's pin
            SerialVE.begin(19200, SERIAL_8N1, cfg.rxPin, -1);
            serialInitialized = true;
        } else if (NUM_DEVICES > 1) {
            // Subsequent switches: use uart_set_pin to remap RX without buffer reallocation
            // UART2 = uart_num 2, only changing RX pin, TX/RTS/CTS stay as-is
            uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, cfg.rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            
            // Flush any leftover data from previous device
            while (SerialVE.available()) {
                SerialVE.read();
            }
        }
        
        // Clear old data completely to prevent stale data mixing between devices
        // veEnd controls how many entries are valid, but frameEndEvent uses <= comparison
        // so we also clear the first entry's name to be safe
        frameHandler.veEnd = 0;
        frameHandler.veData[0].veName[0] = '\0';
        
        logger->logDebug(GwLog::DEBUG, "VE.Direct: reading %s (GPIO %d)", cfg.name, cfg.rxPin);
        
        // Wait for data with timeout
        unsigned long startTime = millis();
        bool gotData = false;
        
        while ((millis() - startTime) < frameTimeout) {
            while (SerialVE.available()) {
                frameHandler.rxData(SerialVE.read());
                gotData = true;
            }
            
            // Check if we got a complete frame
            if (gotData && frameHandler.veEnd > 0) {
                break;
            }
            delay(10);
        }
        
        // Send N2K messages if we got data
        if (frameHandler.veEnd > 0) {
            sendN2kMessages(api, logger, cfg.batteryInstance, cfg.name);
        } else {
            logger->logDebug(GwLog::DEBUG, "VE.Direct %s: no data", cfg.name);
        }
        
        // Move to next device
        currentDevice = (currentDevice + 1) % NUM_DEVICES;
        
        // Small delay between devices (only matters for single device)
        if (NUM_DEVICES == 1) {
            delay(3000);  // Wait before next read cycle
        }
        
            // You can also check the current task (the one calling this)
            logger->logDebug(GwLog::DEBUG, "VE DIRECT Current:    %u bytes remaining\n", uxTaskGetStackHighWaterMark(NULL));
    }


}

void veDirectInit(GwApi *api)
{
    api->addUserTask(veDirectTask, "veDirectTask", 3072);
}

#endif