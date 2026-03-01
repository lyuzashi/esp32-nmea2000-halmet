/**
 * Stub GwXDRMappings.h for Halmet build
 * 
 * This replaces lib/xdrmappings/GwXDRMappings.h when XDR transducer mapping
 * is stripped out via lib_ignore.
 * 
 * USAGE:
 *   1. Add "xdrmappings" to lib_ignore in platformio.ini
 *   2. This stub will be used instead, providing no-op implementations
 */
#ifndef _GWXDRMAPPINGS_H
#define _GWXDRMAPPINGS_H

#include "GwLog.h"
#include <WString.h>

// Forward declarations
class GwConfigHandler;
class GwBoatData;

// Minimal enum needed for API compatibility
typedef enum {
    XDRTEMP=0,
    XDRHUMIDITY=1,
    XDRPRESSURE=2,
    XDRTIME=3,
    XDRFLUID=4,
    XDRDCTYPE=5,
    XDRBATTYPE=6,
    XDRBATCHEM=7,
    XDRGEAR=8,
    XDRBAT=9,
    XDRENGINE=10,
    XDRATTITUDE=11
} GwXDRCategory;

// Minimal GwXDRType stub
class GwXDRType {
public:
    typedef enum {
        PRESS=0, PERCENT=1, VOLT=2, AMP=3, TEMP=4, HUMID=5,
        VOLPERCENT=6, VOLUME=7, FLOW=8, GENERIC=9, DISPLACEMENT=10,
        RPM=11, DISPLACEMENTD=12, UNKNOWN=99
    } TypeCode;
};

// Minimal GwXDRMappingDef stub for addFixedMapping API
class GwXDRMappingDef {
public:
    typedef enum { IS_SINGLE=0, IS_IGNORE, IS_AUTO, IS_LAST } InstanceMode;
    typedef enum { M_DISABLED=0, M_BOTH=1, M_TO2K=2, M_FROM2K=3, M_LAST } Direction;
    
    String xdrName;
    GwXDRCategory category;
    int selector = -1;
    int field = 0;
    InstanceMode instanceMode = IS_AUTO;
    int instanceId = -1;
    Direction direction = M_BOTH;
    GwXDRType::TypeCode type = GwXDRType::UNKNOWN;
    
    String toString() const { return xdrName; }
};

// Minimal GwXDRFoundMapping stub
class GwXDRFoundMapping {
public:
    bool empty = true;
};

/**
 * Stub GwXDRMappings class - XDR mapping disabled
 * All methods are no-ops
 */
class GwXDRMappings {
public:
    GwXDRMappings(GwLog *logger, GwConfigHandler *config) {}
    
    bool addFixedMapping(const GwXDRMappingDef &mapping) { return false; }
    void begin() {}
    
    GwXDRFoundMapping getMapping(String xName, String xType, String xUnit) {
        return GwXDRFoundMapping();
    }
    GwXDRFoundMapping getMapping(double value, GwXDRCategory category, int selector, int field=0, int instance=-1) {
        return GwXDRFoundMapping();
    }
    
    String getXdrEntry(String mapping, double value, int instance=0) {
        return String("");
    }
    
    const char* getUnMapped() {
        return "";
    }
};

#endif // _GWXDRMAPPINGS_H
