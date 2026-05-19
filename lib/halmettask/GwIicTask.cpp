/**
 * Minimal I2C Micro-Task for Halmet
 * 
 * Simply:
 * - Initializes I2C bus with configured pins (deferred to first micro-task call)
 * - Polls registered sensors at their intervals via GwIntervalRunner
 * - Sensors handle their own N2K message sending
 * 
 * Runs as a micro-task (called every ~5s by halmetTask) rather than a 
 * dedicated FreeRTOS task, saving ~3KB of stack memory.
 */
#include "GwIicTask.h"
#include "GwHardware.h"  // Defines _GWIIC when GWIIC_SCL is set

#ifdef BOARD_HALMET
#ifdef HALMET_IIC_ENABLED
#ifdef _GWIIC

#include "GwLog.h"
#include "GwHalmetSensor.h"
#include "GwTimer.h"
#include "GwApi.h"
#include "GwHalmetTask.h"
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

// Static state (NO api storage - passed to micro-task)
static TwoWire *g_bus1 = nullptr;
static TwoWire *g_bus2 = nullptr;
static GwIntervalRunner *g_timers = nullptr;
static int g_counterId = -1;
static GwApi *g_currentApi = nullptr;  // Only valid during micro-task execution
static HalmetSensorList g_sensors;     // Copy of sensors for micro-task use

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

/**
 * Deferred hardware initialization - called on first micro-task run.
 */
static bool initHardware(GwApi *api) {
    GwLog *logger = api->getLogger();
    
    // Get sensors
    int res = -1;
    auto sensorData = api->taskInterfaces()->get<ConfiguredHalmetSensors>(res);
    g_sensors = sensorData.sensors;
    
    // Initialize buses
    if (initBus(logger, Wire, 1, GWIIC_SDA, GWIIC_SCL)) {
        g_bus1 = &Wire;
    }
    if (initBus(logger, Wire1, 2, GWIIC_SDA2, GWIIC_SCL2)) {
        g_bus2 = &Wire1;
    }
    
    // Create timer runner
    g_timers = new GwIntervalRunner();
    g_counterId = api->addCounter("sensors");
    int activeCount = 0;
    
    for (auto &sensor : g_sensors) {
        TwoWire *wire = (sensor->busId == 2) ? g_bus2 : g_bus1;
        
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
        
        // Capture sensor and wire for lambda - api comes from g_currentApi during loop()
        HalmetSensorPtr s = sensor;
        if (sensor->useSamplePipeline()) {
            long sampleMs = sensor->sampleIntervalMs > 0 ? sensor->sampleIntervalMs : 1000;
            long publishMs = sensor->intervalMs > 0 ? sensor->intervalMs : 10000;

            g_timers->addAction(sampleMs, [wire, s]() {
                if (g_currentApi) {
                    s->sample(g_currentApi, wire);
                }
            });
            g_timers->addAction(publishMs, [s]() {
                if (g_currentApi) {
                    s->publish(g_currentApi, g_counterId);
                }
            });
            LOG_DEBUG(GwLog::LOG, "%s: sampling=%ldms publish=%ldms",
                      sensor->name.c_str(), sampleMs, publishMs);
        } else {
            g_timers->addAction(sensor->intervalMs, [wire, s]() {
                if (g_currentApi) {
                    s->measure(g_currentApi, wire, g_counterId);
                }
            });
        }
        activeCount++;
    }
    
    if (activeCount == 0) {
        LOG_DEBUG(GwLog::LOG, "No active sensors");
        delete g_timers;
        g_timers = nullptr;
        return false;
    }
    
    return true;
}

void initIicTask(GwApi *api) {
    GwLog *logger = api->getLogger();
    LOG_DEBUG(GwLog::LOG, "IIC micro-task init");
    
    // Check for registered sensors before registering micro-task
    int res = -1;
    auto sensorData = api->taskInterfaces()->get<ConfiguredHalmetSensors>(res);
    
    if (sensorData.sensors.empty()) {
        LOG_DEBUG(GwLog::LOG, "No IIC sensors registered");
        return;
    }
    
    LOG_DEBUG(GwLog::LOG, "IIC: %d sensor(s), will init on first micro-task call", sensorData.sensors.size());
    
    // Register micro-task with init callback
    halmetRegisterMicroTask("IIC", 
        // Periodic task
        [](GwApi* api) {
            if (!g_timers) return;
            
            // Set current api for timer callbacks, run timers, clear api
            g_currentApi = api;
            g_timers->loop();
            g_currentApi = nullptr;
        },
        // One-time init callback
        initHardware
    );
}

#else  // _GWIIC not defined

void initIicTask(GwApi *api) {
    api->getLogger()->logDebug(GwLog::DEBUG, "IIC disabled (no pins configured)");
}

#endif  // _GWIIC
#endif  // HALMET_IIC_ENABLED
#endif  // BOARD_HALMET
