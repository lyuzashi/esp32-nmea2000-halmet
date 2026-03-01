/**
 * Stub NMEA0183DataToN2K.h for Halmet build
 * 
 * This replaces lib/nmea0183ton2k/NMEA0183DataToN2K.h when NMEA0183→N2K conversion
 * is stripped out via lib_ignore.
 * 
 * USAGE:
 *   1. Add "nmea0183ton2k" to lib_ignore in platformio.ini
 *   2. This stub will be used instead, providing no-op implementations
 */
#ifndef _NMEA0183DATATON2K_H
#define _NMEA0183DATATON2K_H

#include "GwLog.h"
#include "GwBoatData.h"
#include "N2kMessages.h"
#include "GwConverterConfig.h"

// Forward declarations
class GwXDRMappings;

class NMEA0183DataToN2K {
public:
    typedef bool (*N2kSender)(const tN2kMsg &msg, int sourceId);

protected:
    unsigned long lastRmc = 0;

public:
    NMEA0183DataToN2K() : lastRmc(0) {}
    
    // No-op implementations
    virtual bool parseAndSend(const char *buffer, int sourceId) { return false; }
    virtual unsigned long *handledPgns() { 
        static unsigned long emptyPgns[] = {0}; 
        return emptyPgns; 
    }
    virtual int numConverters() { return 0; }
    virtual String handledKeys() { return String(""); }
    unsigned long getLastRmc() const { return lastRmc; }
    
    /**
     * Factory method - returns a stub instance
     */
    static NMEA0183DataToN2K* create(
        GwLog *logger,
        GwBoatData *boatData,
        N2kSender callback,
        GwXDRMappings *xdrMappings,
        const GwConverterConfig &config
    ) {
        static NMEA0183DataToN2K stubInstance;
        return &stubInstance;
    }
};

#endif // _NMEA0183DATATON2K_H
