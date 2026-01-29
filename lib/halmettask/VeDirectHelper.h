#ifndef _VEDIRECT_HELPER_H
#define _VEDIRECT_HELPER_H

#include <Arduino.h>
#include "VeDirectFrameHandler.h"
#include <map>
#include <functional>

// Forward declaration
class GwApi;

/**
 * Helper class to provide efficient name-based lookups for VeDirectFrameHandler data
 * Uses a map to cache index positions for O(log n) lookup instead of O(n) linear search
 */
class VeDirectHelper {
private:
    VeDirectFrameHandler* handler;
    std::map<String, int> nameToIndexMap;
    int lastKnownCount;
    
    // Update the map when new data arrives
    void updateMap() {
        if (handler->veEnd != lastKnownCount) {
            nameToIndexMap.clear();
            for (int i = 0; i < handler->veEnd; i++) {
                if (handler->veData[i].veName[0] != '\0') {
                    nameToIndexMap[String(handler->veData[i].veName)] = i;
                }
            }
            lastKnownCount = handler->veEnd;
        }
    }

public:
    VeDirectHelper(VeDirectFrameHandler* veHandler) 
        : handler(veHandler), lastKnownCount(0) {
    }
    
    /**
     * Get value by name - returns pointer to value string or nullptr if not found
     * Call this after rxData() has processed incoming data
     */
    const char* getValue(const char* name) {
        updateMap();
        auto it = nameToIndexMap.find(String(name));
        if (it != nameToIndexMap.end()) {
            return handler->veData[it->second].veValue;
        }
        return nullptr;
    }
    
    /**
     * Get value as double - returns the parsed value or defaultValue if not found
     */
    double getValueAsDouble(const char* name, double defaultValue = 0.0) {
        const char* value = getValue(name);
        if (value != nullptr) {
            return atof(value);
        }
        return defaultValue;
    }
    
    /**
     * Get value as int - returns the parsed value or defaultValue if not found
     */
    int getValueAsInt(const char* name, int defaultValue = 0) {
        const char* value = getValue(name);
        if (value != nullptr) {
            return atoi(value);
        }
        return defaultValue;
    }
    
    /**
     * Check if a specific field exists
     */
    bool hasValue(const char* name) {
        updateMap();
        return nameToIndexMap.find(String(name)) != nameToIndexMap.end();
    }
};

#endif
