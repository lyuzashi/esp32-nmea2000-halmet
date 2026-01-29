#ifndef _GWBOATDATA_H
#define _GWBOATDATA_H

// Minimal stub for GwBoatData when excluded from build (halmet environment)
#include "GwLog.h"
#include "GWConfig.h"
#include <Arduino.h>

class GwJsonDocument;

// Minimal stub for boat item base
class GwBoatItemBase {
public:
    GwBoatItemBase() {}
    virtual ~GwBoatItemBase() {}
    bool isValid() const { return false; }
    const char *getDataString() { return ""; }
    String getName() { return ""; }
    const String &getFormat() const { static String empty; return empty; }
    virtual double getDoubleValue() { return 0.0; }
    virtual int getLastSource() { return -1; }
};

// Minimal stub for GwBoatItemNameProvider
class GwBoatItemNameProvider {
public:
    virtual String getBoatItemName() = 0;
    virtual String getBoatItemFormat() = 0;
    virtual unsigned long getInvalidTime() { return 60000; }
    virtual ~GwBoatItemNameProvider() {}
};

// Minimal stub for GwBoatData
class GwBoatData {
public:
    GwBoatData(GwLog *logger, GwConfigHandler *cfg) {}
    ~GwBoatData() {}
    
    void begin() {}
    
    bool isValid(String name) { return false; }
    double getDoubleValue(String name, double defaultv) { return defaultv; }
    GwBoatItemBase *getBase(String name) { return nullptr; }
    
    String toJson() const { return "{\"status\":\"boatData disabled\"}"; }
    String toString() { return "boatData disabled"; }
};

#endif // _GWBOATDATA_H
