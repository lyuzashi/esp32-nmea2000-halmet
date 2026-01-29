#ifndef _GWXDRMAPPINGS_H
#define _GWXDRMAPPINGS_H

// Minimal stub for GwXDRMappings when excluded from build (halmet environment)
#include "GwLog.h"
#include "GwBoatData.h"
#include <WString.h>
#include <vector>
#include <map>

// XDR category enum
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
        PRESS=0,
        PERCENT=1,
        VOLT=2,
        AMP=3,
        TEMP=4,
        HUMID=5,
        VOLPERCENT=6,
        VOLUME=7,
        FLOW=8,
        GENERIC=9,
        DISPLACEMENT=10,
        RPM=11,
        DISPLACEMENTD=12,
        UNKNOWN=99
    } TypeCode;
};

// Minimal GwXDRTypeMapping stub
class GwXDRTypeMapping {
public:
    GwXDRCategory category;
    int fieldIndex;
    GwXDRType::TypeCode type;
    
    GwXDRTypeMapping(int category, int fieldIndex, int type) 
        : category((GwXDRCategory)category),
          type((GwXDRType::TypeCode)type),
          fieldIndex(fieldIndex) {}
};

// Minimal GwXDRMappingDef stub
class GwXDRMappingDef {
public:
    typedef enum {
        IS_SINGLE=0,
        IS_IGNORE,
        IS_AUTO,
        IS_LAST
    } InstanceMode;
    
    typedef enum {
        M_DISABLED=0,
        M_BOTH=1,
        M_TO2K=2,
        M_FROM2K=3,
        M_LAST
    } Direction;
    
    String xdrName;
    GwXDRCategory category;
    int selector=-1;
    int field=0;
    InstanceMode instanceMode=IS_AUTO;
    int instanceId=-1;
    Direction direction=M_BOTH;
    
    GwXDRMappingDef(String xdrName, GwXDRCategory category,
        int selector, int field, InstanceMode instanceMode, int instance,
        Direction direction)
        : xdrName(xdrName), category(category), selector(selector),
          field(field), instanceMode(instanceMode), instanceId(instance),
          direction(direction) {}
    
    GwXDRMappingDef() : category(XDRTEMP) {}
    
    String toString() const { return ""; }
    String getTransducerName(int instance) const { return xdrName; }
};

// Minimal GwXDRFoundMapping stub
class GwXDRFoundMapping : public GwBoatItemNameProvider {
public:
    GwXDRFoundMapping() {}
    virtual String getTransducerName() { return ""; }
    virtual String getBoatItemName() { return ""; }
    virtual String getBoatItemFormat() { return ""; }
    virtual ~GwXDRFoundMapping() {}
};

// Minimal GwXDRMappings stub
class GwConfigHandler;
class GwXDRMappings {
public:
    GwXDRMappings(GwLog *logger, GwConfigHandler *config) {}
    
    bool addFixedMapping(const GwXDRMappingDef &mapping) { return true; }
    void begin() {}
    
    GwXDRFoundMapping getMapping(String xName, String xType, String xUnit) {
        return GwXDRFoundMapping();
    }
    
    GwXDRFoundMapping getMapping(GwXDRCategory category, int selector, int field=0, int instance=-1) {
        return GwXDRFoundMapping();
    }
    
    String getXdrEntry(String mapping, double value, int instance=0) { return ""; }
    const char * getUnMapped() { return ""; }
    const GwXDRType * findType(const String &typeString, const String &unitString) const {
        return nullptr;
    }
};

#endif // _GWXDRMAPPINGS_H
