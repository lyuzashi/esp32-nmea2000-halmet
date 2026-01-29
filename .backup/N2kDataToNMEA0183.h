#ifndef _N2KDATATONMEA0183_H
#define _N2KDATATONMEA0183_H

// Minimal stub for N2kDataToNMEA0183 when excluded from build (halmet environment)
#include <functional>
#include <NMEA0183.h>
#include <NMEA2000.h>
#include "GwLog.h"
#include "GwBoatData.h"
#include "GwXDRMappings.h"
#include "GwConverterConfig.h"

class GwJsonDocument;

class N2kDataToNMEA0183 {
public:
    typedef std::function<void(const tNMEA0183Msg &NMEA0183Msg, int id)> SendNMEA0183MessageCallback;
    
protected:
    GwConverterConfig config;
    GwLog *logger;
    GwBoatData *boatData;
    int sourceId = 0;
    char talkerId[3];
    SendNMEA0183MessageCallback sendNMEA0183MessageCallback;
    
    void SendMessage(const tNMEA0183Msg &NMEA0183Msg) {}
    
    N2kDataToNMEA0183(GwLog *logger, GwBoatData *boatData,  
        SendNMEA0183MessageCallback callback, String talkerId)
        : logger(logger), boatData(boatData), sourceId(0),
          sendNMEA0183MessageCallback(callback) {
        strncpy(this->talkerId, talkerId.c_str(), 2);
        this->talkerId[2] = 0;
    }

public:
    static N2kDataToNMEA0183* create(GwLog *logger, GwBoatData *boatData, 
        SendNMEA0183MessageCallback callback, String talkerId, 
        GwXDRMappings *xdrMappings, const GwConverterConfig &cfg) {
        return new N2kDataToNMEA0183(logger, boatData, callback, talkerId);
    }
    
    virtual void HandleMsg(const tN2kMsg &N2kMsg, int sourceId) {}
    virtual void loop(unsigned long lastRmc) {}
    virtual ~N2kDataToNMEA0183() {}
    
    virtual unsigned long* handledPgns() {
        static unsigned long pgns[] = {0};
        return pgns;
    }
    virtual int numPgns() { return 0; }
    virtual void toJson(GwJsonDocument *json) {}
    virtual String handledKeys() { return ""; }
};

#endif // _N2KDATATONMEA0183_H
