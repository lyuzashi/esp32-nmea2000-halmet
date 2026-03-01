/**
 * AM2320 Temperature/Humidity Sensor for Halmet
 * 
 * Simple implementation - reads sensor, sends N2K message.
 */
#include "GwHalmetAM2320task.h"

#ifdef BOARD_HALMET
#ifdef AM2320_ENABLED
#ifdef _GWIIC

#include "GwHalmetSensor.h"
#include "GwApi.h"
#include "GwLog.h"
#include "N2kMessages.h"
#include <Adafruit_AM2320.h>

class AM2320Sensor : public HalmetSensor {
public:
    Adafruit_AM2320 *device = nullptr;
    int iid = 99;  // N2K instance ID
    tN2kTempSource tempSource = N2kts_InsideTemperature;
    tN2kHumiditySource humidSource = N2khs_InsideHumidity;
    
    AM2320Sensor() : HalmetSensor("AM2320") {
        busId = 1;
        addr = 0x5C;
        intervalMs = 10000;  // 10 seconds default
    }
    
    bool init(GwApi *api, TwoWire *wire) override {
        GwLog *logger = api->getLogger();
        
        device = new Adafruit_AM2320(wire);
        if (!device->begin()) {
            LOG_DEBUG(GwLog::ERROR, "AM2320: init failed at 0x%02X", addr);
            delete device;
            device = nullptr;
            return false;
        }
        
        LOG_DEBUG(GwLog::LOG, "AM2320: ready at 0x%02X", addr);
        return true;
    }
    
    void measure(GwApi *api, TwoWire *wire, int counterId) override {
        if (!device) return;
        
        GwLog *logger = api->getLogger();
        
        float tempC = device->readTemperature();
        float humidity = device->readHumidity();
        
        if (isnan(tempC) || isnan(humidity)) {
            LOG_DEBUG(GwLog::DEBUG, "AM2320: read error");
            return;
        }
        
        double tempK = CToKelvin(tempC);
        
        LOG_DEBUG(GwLog::DEBUG, "AM2320: %.1f°C, %.0f%%", tempC, humidity);
        
        // Send temperature
        tN2kMsg msg;
        SetN2kTemperatureExt(msg, 1, iid, tempSource, tempK, N2kDoubleNA);
        api->sendN2kMessage(msg);
        api->increment(counterId, "AM2320temp");
        
        // Send humidity  
        tN2kMsg msg2;
        SetN2kHumidity(msg2, 1, iid, humidSource, humidity);
        api->sendN2kMessage(msg2);
        api->increment(counterId, "AM2320hum");
    }
};

// Register the sensor
void am2320TaskInit(GwApi *api) {
    GwLog *logger = api->getLogger();
    LOG_DEBUG(GwLog::LOG, "AM2320: registering");
    
    auto sensor = std::make_shared<AM2320Sensor>();
    
    // Register with I2C task via update()
    api->taskInterfaces()->update<ConfiguredHalmetSensors>([sensor](ConfiguredHalmetSensors *list) {
        list->sensors.push_back(sensor);
        return true;
    });
    
    LOG_DEBUG(GwLog::LOG, "AM2320: registered");
}

DECLARE_INITFUNCTION(am2320TaskInit);

#endif  // _GWIIC
#endif  // AM2320_ENABLED
#endif  // BOARD_HALMET
