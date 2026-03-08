/**
 * Minimal I2C Task for Halmet
 * 
 * Simply:
 * - Initializes I2C bus with configured pins
 * - Polls registered sensors at their intervals
 * - Sensors handle their own N2K message sending
 */
#include "GwIicTask.h"
#include "GwHardware.h"  // Defines _GWIIC when GWIIC_SCL is set

#ifdef BOARD_HALMET
#ifdef _GWIIC

#include "GwLog.h"
#include "GwHalmetSensor.h"
#include "GwTimer.h"
#include "GwApi.h"
#include <Wire.h>

// I2C pins - set via build_flags: -D GWIIC_SDA=21 -D GWIIC_SCL=22
#ifndef GWIIC_SDA
    #define GWIIC_SDA -1
#endif
#ifndef GWIIC_SCL
    #define GWIIC_SCL -1
#endif
#ifndef GWIIC_SDA2
    #define GWIIC_SDA2 -1
#endif
#ifndef GWIIC_SCL2
    #define GWIIC_SCL2 -1
#endif

static void runIicTask(GwApi *api);

// Initialize an I2C bus
static bool initBus(GwLog *logger, TwoWire &wire, int num, int sda, int scl) {
    if (sda < 0 || scl < 0) {
        LOG_DEBUG(GwLog::DEBUG, "IIC %d not configured (sda=%d, scl=%d)", num, sda, scl);
        return false;
    }
    if (!wire.begin(sda, scl)) {
        LOG_DEBUG(GwLog::ERROR, "IIC %d init failed (sda=%d, scl=%d)", num, sda, scl);
        return false;
    }
    LOG_DEBUG(GwLog::LOG, "IIC %d ready (sda=%d, scl=%d)", num, sda, scl);
    return true;
}

void initIicTask(GwApi *api) {
    GwLog *logger = api->getLogger();
    LOG_DEBUG(GwLog::LOG, "IIC task init");
    
    // Check for registered sensors
    int res = -1;
    auto sensorData = api->taskInterfaces()->get<ConfiguredHalmetSensors>(res);
    
    if (sensorData.sensors.empty()) {
        LOG_DEBUG(GwLog::LOG, "No IIC sensors registered");
        return;
    }
    
    LOG_DEBUG(GwLog::LOG, "Starting IIC task with %d sensor(s)", sensorData.sensors.size());
    api->addUserTask(runIicTask, "iicTask", 4000);
}

static void runIicTask(GwApi *api) {
    GwLog *logger = api->getLogger();
    
    // Get sensors
    int res = -1;
    auto sensorData = api->taskInterfaces()->get<ConfiguredHalmetSensors>(res);
    
    // Initialize buses
    TwoWire *bus1 = nullptr;
    TwoWire *bus2 = nullptr;
    
    if (initBus(logger, Wire, 1, GWIIC_SDA, GWIIC_SCL)) {
        bus1 = &Wire;
    }
    if (initBus(logger, Wire1, 2, GWIIC_SDA2, GWIIC_SCL2)) {
        bus2 = &Wire1;
    }
    
    // Initialize sensors and set up polling
    GwIntervalRunner timers;
    int counterId = api->addCounter("sensors");
    int activeCount = 0;
    
    for (auto &sensor : sensorData.sensors) {
        TwoWire *wire = (sensor->busId == 2) ? bus2 : bus1;
        
        if (!wire) {
            LOG_DEBUG(GwLog::ERROR, "%s: bus %d not available", 
                      sensor->name.c_str(), sensor->busId);
            continue;
        }
        
        if (!sensor->init(api, wire)) {
            LOG_DEBUG(GwLog::ERROR, "%s: init failed", sensor->name.c_str());
            continue;
        }
        
        LOG_DEBUG(GwLog::LOG, "%s: active, interval=%ldms", 
                  sensor->name.c_str(), sensor->intervalMs);
        
        // Capture for lambda
        HalmetSensorPtr s = sensor;
        timers.addAction(sensor->intervalMs, [api, wire, s, counterId]() {
            s->measure(api, wire, counterId);
        });
        activeCount++;
    }
    
    if (activeCount == 0) {
        LOG_DEBUG(GwLog::LOG, "No active sensors, exiting");
        vTaskDelete(NULL);
        return;
    }
    
    // Polling loop
    while (true) {
        delay(100);
        timers.loop();
    }
}

#else  // _GWIIC not defined

void initIicTask(GwApi *api) {
    api->getLogger()->logDebug(GwLog::DEBUG, "IIC disabled (no pins configured)");
}

#endif  // _GWIIC
#endif  // BOARD_HALMET
