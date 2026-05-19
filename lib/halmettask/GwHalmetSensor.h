/**
 * Simple I2C Sensor interface for Halmet
 * 
 * Minimal interface - sensors just need to:
 * 1. Initialize their device on a Wire bus
 * 2. Read values and send N2K messages
 */
#ifndef _GWHALMET_SENSOR_H
#define _GWHALMET_SENSOR_H

#ifdef HALMET_SENSOR_ENABLED

#include <Arduino.h>
#include <Wire.h>
#include <memory>
#include <vector>
#include "GwApi.h"

/**
 * Simple sensor interface - implement this for each sensor type
 */
class HalmetSensor {
public:
    String name;           // Sensor name for logging
    int busId = 1;         // Which I2C bus (1 = Wire, 2 = Wire1)
    int addr = 0;          // I2C address
    long intervalMs = 10000;  // Polling interval in milliseconds
    long sampleIntervalMs = 1000; // Sampling interval for pipeline mode
    
    HalmetSensor(const String &sensorName) : name(sensorName) {}
    virtual ~HalmetSensor() {}
    
    // Initialize the sensor hardware. Return true if successful.
    virtual bool init(GwApi *api, TwoWire *wire) = 0;
    
    // Read sensor and send N2K message(s). Called at intervalMs.
    virtual void measure(GwApi *api, TwoWire *wire, int counterId) = 0;

    // Optional pipeline mode for averaging/filtering.
    // Default keeps old behavior and preserves compatibility.
    virtual bool useSamplePipeline() const { return false; }
    virtual void sample(GwApi *api, TwoWire *wire) {
        // Legacy fallback: read+send in one step.
        measure(api, wire, -1);
    }
    virtual void publish(GwApi *api, int counterId) {
        // Legacy fallback: no-op; measure() path handles publishing.
        (void)api;
        (void)counterId;
    }
};

using HalmetSensorPtr = std::shared_ptr<HalmetSensor>;
using HalmetSensorList = std::vector<HalmetSensorPtr>;

/**
 * Task interface for registering sensors with the I2C task
 */
class ConfiguredHalmetSensors : public GwApi::TaskInterfaces::Base {
public:
    HalmetSensorList sensors;
};

DECLARE_TASKIF(ConfiguredHalmetSensors);

#endif // HALMET_SENSOR_ENABLED

#endif // _GWHALMET_SENSOR_H
