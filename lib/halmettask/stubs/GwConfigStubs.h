/**
 * Stub constants for filtered converter config items
 * 
 * When the "converter" category is filtered from config.json,
 * these constants are removed from GwConfigDefinitions.h.
 * This header provides them so main.cpp compiles.
 * 
 * Include this AFTER GWConfig.h to add the missing members.
 */
#ifndef _GWCONFIGSTUBS_H
#define _GWCONFIGSTUBS_H

// These constants are normally in GwConfigDefinitions but get filtered out
// when "converter" category is removed. Provide stubs here.
// The actual config values won't exist, so getString/getBool will return defaults.
namespace GwConfigStubs {
    static constexpr const char* talkerId = "talkerId";
    static constexpr const char* sendN2k = "sendN2k";
    static constexpr const char* minXdrInterval = "minXdrInterval";
    static constexpr const char* min2KInterval = "min2KInterval";
    static constexpr const char* unknownXdr = "unknownXdr";
    static constexpr const char* sendRMCi = "sendRMCi";
    static constexpr const char* checkRMCt = "checkRMCt";
    static constexpr const char* stbRudderI = "stbRudderI";
    static constexpr const char* portRudderI = "portRudderI";
    static constexpr const char* winst312 = "winst312";
    static constexpr const char* timoSensor = "timoSensor";
}

#endif
