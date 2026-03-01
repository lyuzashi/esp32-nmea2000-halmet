/**
 * Stub GwBoatData.h for Halmet gateway build
 * 
 * Minimal implementation for gateway mode - passes N2K messages through
 * without storing/processing boat data locally.
 * 
 * USAGE:
 *   1. Add "boatData" to lib_ignore in platformio.ini
 *   2. This stub provides no-op implementations
 */
#ifndef _GWBOATDATA_H
#define _GWBOATDATA_H

#include "GwLog.h"
#include "GWConfig.h"
#include <Arduino.h>

#define GW_BOAT_VALUE_LEN 32
#define GWSC(name) static constexpr const char* name=#name
#define ROT_WA_FACTOR 60

class GwJsonDocument;
class GwBoatData;

/**
 * Minimal GwBoatItemBase - just enough interface for compilation
 */
class GwBoatItemBase {
public:
    using TOType = enum { def=1, ais=2, sensor=3, lng=4, user=5, keep=6 };
    
    static const long INVALID_TIME = 60000;
    
    // Formatter names (needed for API compatibility)
    GWSC(formatCourse);
    GWSC(formatKnots);
    GWSC(formatWind);
    GWSC(formatLatitude);
    GWSC(formatLongitude);
    GWSC(formatXte);
    GWSC(formatFixed0);
    GWSC(formatDepth);
    GWSC(kelvinToC);
    GWSC(mtr2nm);
    GWSC(formatDop);
    GWSC(formatRot);
    GWSC(formatDate);
    GWSC(formatTime);

protected:
    String name;
    String format;
    int lastUpdateSource = 0;

public:
    GwBoatItemBase(String n, String f, TOType t) : name(n), format(f) {}
    GwBoatItemBase(String n, String f, unsigned long inv) : name(n), format(f) {}
    virtual ~GwBoatItemBase() {}
    
    bool isValid(unsigned long now = 0) const { return false; }
    void invalidate() {}
    const char* getDataString() { return ""; }
    virtual void fillString() {}
    virtual void toJsonDoc(GwJsonDocument *doc, unsigned long minTime) {}
    virtual size_t getJsonSize() { return 0; }
    virtual int getLastSource() { return lastUpdateSource; }
    virtual void refresh(unsigned long ts = 0) {}
    virtual double getDoubleValue() { return 0.0; }
    String getName() { return name; }
    const String& getFormat() const { return format; }
    virtual void setInvalidTime(GwConfigHandler *cfg) {}
    TOType getToType() { return TOType::def; }
    int getCurrentType() { return 0; }
    unsigned long getLastSet() const { return 0; }
    
    class GwBoatItemMap {
        GwBoatData *boatData;
    public:
        GwBoatItemMap(GwBoatData *bd) : boatData(bd) {}
        void add(const String &name, GwBoatItemBase *item) {}
    };
};

/**
 * Minimal GwBoatItem template - stub implementation
 */
template<class T> 
class GwBoatItem : public GwBoatItemBase {
protected:
    T data{};
public:
    GwBoatItem(String name, String formatInfo, unsigned long invalidTime = INVALID_TIME, GwBoatItemMap *map = NULL)
        : GwBoatItemBase(name, formatInfo, invalidTime) {}
    GwBoatItem(String name, String formatInfo, TOType toType, GwBoatItemMap *map = NULL)
        : GwBoatItemBase(name, formatInfo, toType) {}
    virtual ~GwBoatItem() {}
    
    bool update(T nv, int source) { return false; }
    bool updateMax(T nv, int sourceId) { return false; }
    T getData() { return data; }
    T getDataWithDefault(T defaultv) { return defaultv; }
    virtual double getDoubleValue() { return 0.0; }
    virtual void fillString() {}
    virtual void toJsonDoc(GwJsonDocument *doc, unsigned long minTime) {}
};

// Conversion functions (no-op stubs)
inline double formatCourse(double cv) { return cv; }
inline double formatDegToRad(double deg) { return deg; }
inline double formatWind(double cv) { return cv; }
inline double formatKnots(double cv) { return cv; }
inline double formatKmh(double cv) { return cv; }
inline uint32_t mtr2nm(uint32_t m) { return m; }
inline double mtr2nm(double m) { return m; }

/**
 * Stub satellite info classes
 */
class GwSatInfo {
public:
    unsigned char PRN = 0;
    uint32_t Elevation = 0;
    uint32_t Azimut = 0;
    uint32_t SNR = 0;
    unsigned long validTill = 0;
};

class GwSatInfoList {
public:
    static const GwBoatItemBase::TOType toType = GwBoatItemBase::TOType::lng;
    int getNumSats() const { return 0; }
    GwSatInfo* getAt(int idx) { return nullptr; }
    operator double() { return 0; }
};

class GwBoatDataSatList : public GwBoatItem<GwSatInfoList> {
public:
    GwBoatDataSatList(String name, String formatInfo, GwBoatItemBase::TOType toType, GwBoatItemBase::GwBoatItemMap *map = NULL)
        : GwBoatItem(name, formatInfo, toType, map) {}
    bool update(GwSatInfo info, int source) { return false; }
    GwSatInfo* getAt(int idx) { return nullptr; }
    int getNumSats() { return 0; }
};

class GwBoatItemNameProvider {
public:
    virtual String getBoatItemName() = 0;
    virtual String getBoatItemFormat() = 0;
    virtual unsigned long getInvalidTime() { return GwBoatItemBase::INVALID_TIME; }
    virtual ~GwBoatItemNameProvider() {}
};

// Macros to define boat data items (create nullptr stubs)
#define GWBOATDATAT(type, name, toType, fmt) \
    static constexpr const char* _##name = #name; \
    GwBoatItem<type> *name = nullptr;
#define GWBOATDATA(type, name, fmt) GWBOATDATAT(type, name, GwBoatItemBase::TOType::def, fmt)
#define GWSPECBOATDATA(clazz, name, toType, fmt) \
    clazz *name = nullptr;

/**
 * Stub GwBoatData class - gateway mode (no local data storage)
 */
class GwBoatData {
private:
    GwLog *logger = nullptr;
    GwConfigHandler *config = nullptr;
    GwBoatItemBase::GwBoatItemMap values{this};

public:
    // All boat data items are nullptr in stub mode
    GWBOATDATA(double, COG, formatCourse)
    GWBOATDATA(double, SOG, formatKnots)
    GWBOATDATA(double, HDT, formatCourse)
    GWBOATDATA(double, HDM, formatCourse)
    GWBOATDATA(double, STW, formatKnots)
    GWBOATDATA(double, VAR, formatWind)
    GWBOATDATA(double, DEV, formatWind)
    GWBOATDATA(double, AWA, formatWind)
    GWBOATDATA(double, AWS, formatKnots)
    GWBOATDATAT(double, MaxAws, GwBoatItemBase::TOType::keep, formatKnots)
    GWBOATDATA(double, TWD, formatCourse)
    GWBOATDATA(double, TWA, formatWind)
    GWBOATDATA(double, TWS, formatKnots)
    GWBOATDATAT(double, MaxTws, GwBoatItemBase::TOType::keep, formatKnots)
    GWBOATDATA(double, ROT, formatRot)
    GWBOATDATA(double, RPOS, formatWind)
    GWBOATDATA(double, PRPOS, formatWind)
    GWBOATDATA(double, LAT, formatLatitude)
    GWBOATDATA(double, LON, formatLongitude)
    GWBOATDATA(double, ALT, formatFixed0)
    GWBOATDATA(double, HDOP, formatDop)
    GWBOATDATA(double, PDOP, formatDop)
    GWBOATDATA(double, VDOP, formatDop)
    GWBOATDATA(double, DBS, formatDepth)
    GWBOATDATA(double, DBT, formatDepth)
    GWBOATDATA(double, GPST, formatTime)
    GWBOATDATA(uint32_t, GPSD, formatDate)
    GWBOATDATAT(int16_t, TZ, GwBoatItemBase::TOType::lng, formatFixed0)
    GWBOATDATA(double, WTemp, kelvinToC)
    GWBOATDATAT(uint32_t, Log, GwBoatItemBase::TOType::lng, mtr2nm)
    GWBOATDATAT(uint32_t, TripLog, GwBoatItemBase::TOType::lng, mtr2nm)
    GWBOATDATA(double, DTW, mtr2nm)
    GWBOATDATA(double, BTW, formatCourse)
    GWBOATDATA(double, XTE, formatXte)
    GWBOATDATA(double, WPLat, formatLatitude)
    GWBOATDATA(double, WPLon, formatLongitude)
    GWSPECBOATDATA(GwBoatDataSatList, SatInfo, GwSatInfoList::toType, formatFixed0)

public:
    GwBoatData(GwLog *logger, GwConfigHandler *cfg) 
        : logger(logger), config(cfg) {}
    ~GwBoatData() {}
    
    void begin() {}
    
    template<class T> 
    GwBoatItem<T>* getOrCreate(T initial, GwBoatItemNameProvider *provider) {
        return nullptr;
    }
    
    template<class T> 
    bool update(T value, int source, GwBoatItemNameProvider *provider) {
        return false;
    }
    
    template<class T> 
    T getDataWithDefault(T defaultv, GwBoatItemNameProvider *provider) {
        return defaultv;
    }
    
    void setInvalidTime(GwBoatItemBase *item) {}
    bool isValid(String name) { return false; }
    double getDoubleValue(String name, double defaultv) { return defaultv; }
    GwBoatItemBase* getBase(String name) { return nullptr; }
    
    String toJson() const { return String("{}"); }
    String toString() { return String(""); }
};

#endif // _GWBOATDATA_H
