/**
 * Stub GwConverterConfig.h for Halmet build
 * 
 * This replaces lib/config/GwConverterConfig.h when NMEA0183 conversion
 * is stripped out. The stub provides the same interface but with no-op
 * implementations that don't depend on converter-related config items.
 * 
 * USAGE:
 *   1. Add "converter" to custom_filter_categories to remove config UI
 *   2. This stub will be used instead, providing default values
 * 
 * The stub maintains API compatibility so code compiles without changes.
 */
#ifndef _GWCONVERTERCONFIG_H
#define _GWCONVERTERCONFIG_H

#include "GWConfig.h"
#include "N2kTypes.h"
#include <map>
#include <vector>

// Empty wind configs map - no NMEA0183 wind conversion
static std::map<tN2kWindReference,String> windConfigs;

class GwConverterConfig {
public:
    class WindMapping {
    public:
        using Wind0183Type = enum {
            AWA_AWS,
            TWA_TWS,
            TWD_TWS,
            GWA_GWS,
            GWD_GWS
        };
        tN2kWindReference n2kType;  
        Wind0183Type nmea0183Type;
        bool valid = false;
        
        WindMapping() {}
        WindMapping(const tN2kWindReference &n2k, const Wind0183Type &n183)
            : n2kType(n2k), nmea0183Type(n183), valid(true) {}
        WindMapping(const tN2kWindReference &n2k, const String &n183)
            : n2kType(n2k), valid(false) {}
    };
    
    // Default values - no config lookup needed
    int minXdrInterval = 100;
    int starboardRudderInstance = 0; 
    int portRudderInstance = -1;
    int min2KInterval = 50;
    int rmcInterval = 1000;
    int rmcCheckTime = 4000;
    int winst312 = 256;
    bool unmappedXdr = false;
    unsigned long xdrTimeout = 60000;
    std::vector<WindMapping> windMappings;
    
    // No-op init - uses default values above
    void init(GwConfigHandler *config, GwLog *logger) {
        // Stub: keep defaults, don't read from config
    }
    
    // Always return invalid mapping - no wind conversion
    const WindMapping findWindMapping(const tN2kWindReference &n2k) const {
        return WindMapping();
    }
    
    const WindMapping findWindMapping(const WindMapping::Wind0183Type &n183) const {
        return WindMapping();
    }
};

#endif // _GWCONVERTERCONFIG_H
