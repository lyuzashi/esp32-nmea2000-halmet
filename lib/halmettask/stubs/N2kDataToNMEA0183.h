/**
 * Stub N2kDataToNMEA0183.h for Halmet build
 * 
 * This replaces lib/nmea2kto0183/N2kDataToNMEA0183.h when NMEA0183 conversion
 * is stripped out via lib_ignore.
 * 
 * USAGE:
 *   1. Add "nmea2kto0183" to lib_ignore in platformio.ini
 *   2. This stub will be used instead, providing no-op implementations
 * 
 * The stub maintains API compatibility with the real class so main.cpp
 * compiles without changes. All methods are no-ops that do nothing.
 */
#ifndef _N2KDATATONMEA0183_H
#define _N2KDATATONMEA0183_H

#include <functional>
#include <N2kMsg.h>
#include <NMEA0183Msg.h>
#include "GwConverterConfig.h"

// Constant needed by main.cpp XdrExampleRequest
#ifndef MAX_NMEA0183_MSG_BUF_LEN
#define MAX_NMEA0183_MSG_BUF_LEN 164
#endif

// Forward declarations to avoid pulling in heavy headers
class GwLog;
class GwBoatData;
class GwXDRMappings;
class GwJsonDocument;

/**
 * Stub N2kDataToNMEA0183 class - NMEA0183 conversion disabled
 * All methods are no-ops that compile but do nothing at runtime
 */
class N2kDataToNMEA0183 {
public:
    typedef std::function<void(const tNMEA0183Msg &NMEA0183Msg, int id)> SendNMEA0183MessageCallback;

    /**
     * Factory method - returns a stub instance (not nullptr!)
     * The stub does nothing but is safe to call methods on.
     */
    static N2kDataToNMEA0183* create(
        GwLog *logger, 
        GwBoatData *boatData,  
        SendNMEA0183MessageCallback callback, 
        String talkerId, 
        GwXDRMappings *xdrMappings,
        const GwConverterConfig &cfg
    ) {
        // Return a static stub instance - safe to call methods on
        static N2kDataToNMEA0183 stubInstance;
        return &stubInstance;
    }

    // All instance methods are no-ops
    virtual void HandleMsg(const tN2kMsg &N2kMsg, int sourceId) {}
    virtual void loop(unsigned long lastRmc) {}
    virtual unsigned long* handledPgns() { 
        static unsigned long emptyPgns[] = {0}; 
        return emptyPgns; 
    }
    virtual int numPgns() { return 0; }
    virtual void toJson(GwJsonDocument *json) {}
    virtual String handledKeys() { return String(""); }
    virtual ~N2kDataToNMEA0183() {}

public:
    N2kDataToNMEA0183() {}
};

#endif // _N2KDATATONMEA0183_H
