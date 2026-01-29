#ifndef _NMEA0183DATATON2K_H
#define _NMEA0183DATATON2K_H

// Minimal stub for NMEA0183DataToN2K when excluded from build (halmet environment)
#include "GwLog.h"
#include "GwBoatData.h"
#include "N2kMessages.h"
#include "GwXDRMappings.h"
#include "GwConverterConfig.h"

class NMEA0183DataToN2K {
public:
    typedef bool (*N2kSender)(const tN2kMsg &msg, int sourceId);
    
protected:
    GwLog *logger;
    GwBoatData *boatData;
    N2kSender sender;
    GwConverterConfig config;
    unsigned long lastRmc = 0;
    
public:
    NMEA0183DataToN2K(GwLog *logger, GwBoatData *boatData, N2kSender callback)
        : logger(logger), boatData(boatData), sender(callback) {}
    
    virtual ~NMEA0183DataToN2K() {}
    
    virtual bool parseAndSend(const char *buffer, int sourceId) { return false; }
    virtual unsigned long *handledPgns() { 
        static unsigned long pgns[] = {0};
        return pgns; 
    }
    virtual int numConverters() { return 0; }
    virtual String handledKeys() { return ""; }
    unsigned long getLastRmc() const { return lastRmc; }
    
    static NMEA0183DataToN2K* create(GwLog *logger, GwBoatData *boatData, N2kSender callback,
        GwXDRMappings *xdrMappings, const GwConverterConfig &config) {
        return new NMEA0183DataToN2K(logger, boatData, callback);
    }
};

#endif // _NMEA0183DATATON2K_H
