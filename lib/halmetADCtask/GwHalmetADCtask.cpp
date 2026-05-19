/**
 * ADC Tank Input Sensor for Halmet
 * 
 * Reads ADS1115 ADC via I2C and sends Tank Level PGN 127505.
 * Architecture:
 * - Registers as IIC sensors - one logical sensor per tank
 * - Both tank sensors share one ADS1115 device instance
 * - Uses sample/publish pipeline to smooth noise and reduce outliers
 * - Sends N2K PGN 127505 frequently for stateless display compatibility
 * 
 * Uses I2C bus managed by GwIicTask (no separate Wire.begin())
 */
#include "GwHalmetADCtask.h"
#include "GwHardware.h"

#ifdef BOARD_HALMET
#ifdef ADC_TANK_ENABLED
#ifdef _GWIIC  // Requires I2C support

#include "GwApi.h"
#include "GwLog.h"
#include "GwHalmetSensor.h"
#include "N2kMessages.h"
#include <Adafruit_ADS1X15.h>

// ADS1115 I2C address on HALMET
#define ADS1115_ADDR 0x4B

// HALMET voltage divider: 33.3/3.3 = 10.09x
// With GAIN_ONE (±4.096V), actual input range is 0-41.4V
static const float VOLTAGE_DIVIDER_SCALE = 33.3f / 3.3f;

// Tank configuration
struct TankConfig {
    bool enabled = false;
    uint8_t tankIndex = 0;        // 0 for tank1, 1 for tank2
    int channel = 0;              // ADC channel (0-3)
    tN2kFluidType fluidType = N2kft_Fuel;
    uint8_t instance = 0;         // N2K tank instance
    double capacity = 100.0;      // Liters
    float emptyVoltage = 0.0f;    // Voltage at 0%
    float fullVoltage = 10.0f;    // Voltage at 100%
};

/**
 * Shared ADS1115 hardware access.
 */
class ADS1115Shared {
private:
    Adafruit_ADS1115 adc;
    bool initialized = false;

public:
    bool init(GwApi* api, TwoWire* wire) {
        if (initialized) return true;
        adc.setGain(GAIN_ONE);  // ±4.096V range
        if (!adc.begin(ADS1115_ADDR, wire)) {
            api->getLogger()->logDebug(GwLog::ERROR, "ADS1115: init failed at 0x%02X", ADS1115_ADDR);
            return false;
        }
        initialized = true;
        api->getLogger()->logDebug(GwLog::LOG, "ADS1115: ready at 0x%02X", ADS1115_ADDR);
        return true;
    }

    float readVoltage(int channel) {
        if (!initialized || channel < 0 || channel > 3) return 0.0f;
        int16_t rawAdc = adc.readADC_SingleEnded(channel);
        float adcVolts = adc.computeVolts(rawAdc);
        return adcVolts * VOLTAGE_DIVIDER_SCALE;
    }
};

static ADS1115Shared g_ads;

/**
 * One logical tank sensor. Two instances can share one ADS1115.
 */
class ADS1115TankSensor : public HalmetSensor {
private:
    TankConfig cfg;
    static constexpr int MAX_SAMPLES = 8;
    float samples[MAX_SAMPLES] = {0};
    int sampleCount = 0;
    int samplePos = 0;

    static const char* fluidName(tN2kFluidType fluidType) {
        switch (fluidType) {
            case N2kft_Fuel: return "fuel";
            case N2kft_Water: return "water";
            case N2kft_GrayWater: return "gray";
            case N2kft_LiveWell: return "livewell";
            case N2kft_Oil: return "oil";
            case N2kft_BlackWater: return "black";
            default: return "unknown";
        }
    }

    float voltageToLevel(float voltage) const {
        if (cfg.fullVoltage <= cfg.emptyVoltage) return 0.0f;
        float level = (voltage - cfg.emptyVoltage) / (cfg.fullVoltage - cfg.emptyVoltage);
        if (level < 0.0f) level = 0.0f;
        if (level > 1.0f) level = 1.0f;
        return level * 100.0f;
    }

    float filteredAverage() const {
        if (sampleCount <= 0) return 0.0f;
        float minV = samples[0];
        float maxV = samples[0];
        float sum = 0.0f;
        for (int i = 0; i < sampleCount; i++) {
            float v = samples[i];
            sum += v;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        // Trim one min/max when enough samples; keeps outlier resistance cheap.
        if (sampleCount >= 5) {
            sum -= (minV + maxV);
            return sum / (sampleCount - 2);
        }
        return sum / sampleCount;
    }

public:
    ADS1115TankSensor(const TankConfig& tankCfg)
        : HalmetSensor(String("ADS1115-Tank") + String((int)tankCfg.tankIndex + 1)), cfg(tankCfg) {
        addr = ADS1115_ADDR;
        // Publish every 10s; sample faster for smoothing.
        intervalMs = 10000;
        sampleIntervalMs = 100;
    }

    bool useSamplePipeline() const override {
        return true;
    }

    bool init(GwApi* api, TwoWire* wire) override {
        return g_ads.init(api, wire);
    }

    void sample(GwApi* api, TwoWire* wire) override {
        (void)api;
        (void)wire;
        float voltage = g_ads.readVoltage(cfg.channel);
        samples[samplePos] = voltage;
        samplePos = (samplePos + 1) % MAX_SAMPLES;
        if (sampleCount < MAX_SAMPLES) sampleCount++;
    }

    void publish(GwApi* api, int counterId) override {
        if (sampleCount <= 0) return;
        GwLog* logger = api->getLogger();

        float voltage = filteredAverage();
        float level = voltageToLevel(voltage);

        tN2kMsg msg;
        SetN2kFluidLevel(msg, cfg.instance, cfg.fluidType, level, cfg.capacity);
        api->sendN2kMessage(msg);

        if (counterId >= 0) {
            api->increment(counterId, fluidName(cfg.fluidType));
        }

        LOG_DEBUG(GwLog::DEBUG, "%s: %.2fV -> %.1f%% (type=%d)",
                  name.c_str(), voltage, level, cfg.fluidType);
    }

    // Backward compatible path when pipeline is disabled.
    void measure(GwApi* api, TwoWire* wire, int counterId) override {
        sample(api, wire);
        publish(api, counterId);
    }
};

/**
 * Convert fluid type string to N2K enum.
 */
static tN2kFluidType parseFluidType(const String& str) {
    if (str == "Water") return N2kft_Water;
    if (str == "Gray Water") return N2kft_GrayWater;
    if (str == "Live Well") return N2kft_LiveWell;
    if (str == "Oil") return N2kft_Oil;
    if (str == "Black Water") return N2kft_BlackWater;
    return N2kft_Fuel;  // Default
}

/**
 * Load tank configuration from config.
 */
static TankConfig loadTankConfig(GwApi* api, int tankNum) {
    GwLog* logger = api->getLogger();
    GwConfigHandler* config = api->getConfig();
    TankConfig tank;
    char nameBuf[32];
    
    // Tank enable
    snprintf(nameBuf, sizeof(nameBuf), "tank%dEnable", tankNum);
    tank.enabled = config->getBool(String(nameBuf), false);
    
    if (!tank.enabled) return tank;
    
    // Channel (0-based internally, Tank 1 = channel 0, Tank 2 = channel 1)
    tank.tankIndex = tankNum - 1;
    tank.channel = tankNum - 1;
    tank.instance = tankNum - 1;
    
    // Fluid type (list returns string like "Fuel", "Water", etc.)
    snprintf(nameBuf, sizeof(nameBuf), "tank%dFluidType", tankNum);
    String fluidStr = config->getString(String(nameBuf), "Fuel");
    tank.fluidType = parseFluidType(fluidStr);
    
    // Capacity in liters
    snprintf(nameBuf, sizeof(nameBuf), "tank%dCapacity", tankNum);
    tank.capacity = config->getInt(String(nameBuf), 100);
    
    // Voltage calibration (stored as 0.1V units in config)
    snprintf(nameBuf, sizeof(nameBuf), "tank%dEmptyV", tankNum);
    tank.emptyVoltage = config->getInt(String(nameBuf), 0) / 10.0f;
    
    snprintf(nameBuf, sizeof(nameBuf), "tank%dFullV", tankNum);
    tank.fullVoltage = config->getInt(String(nameBuf), 100) / 10.0f;
    
    LOG_DEBUG(GwLog::LOG, "Tank%d: type=%s(%d), capacity=%.0fL, empty=%.1fV, full=%.1fV",
              tankNum, fluidStr.c_str(), tank.fluidType, tank.capacity, 
              tank.emptyVoltage, tank.fullVoltage);
    
    return tank;
}

/**
 * Register ADC sensor with IIC task.
 */
void halmetADCInit(GwApi* api) {
    GwLog* logger = api->getLogger();
    LOG_DEBUG(GwLog::LOG, "ADC: registering");

    TankConfig tank1 = loadTankConfig(api, 1);
    TankConfig tank2 = loadTankConfig(api, 2);

    // Register one logical sensor per tank (shared ADS1115 hardware).
    api->taskInterfaces()->update<ConfiguredHalmetSensors>([tank1, tank2](ConfiguredHalmetSensors* list) {
        if (tank1.enabled) {
            list->sensors.push_back(std::make_shared<ADS1115TankSensor>(tank1));
        }
        if (tank2.enabled) {
            list->sensors.push_back(std::make_shared<ADS1115TankSensor>(tank2));
        }
        return true;
    });

    LOG_DEBUG(GwLog::LOG, "ADC: registered with IIC task");
}

#else  // _GWIIC not defined

void halmetADCInit(GwApi* api) {
    api->getLogger()->logDebug(GwLog::DEBUG, "ADC: disabled (no I2C)");
}

#endif  // _GWIIC
#endif  // ADC_TANK_ENABLED
#endif  // BOARD_HALMET
