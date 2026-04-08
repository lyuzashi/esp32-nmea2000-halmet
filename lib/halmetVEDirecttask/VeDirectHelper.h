#ifndef _VEDIRECT_HELPER_H
#define _VEDIRECT_HELPER_H

#include <Arduino.h>
#include "VeDirectFrameHandler.h"

/**
 * Helper class for name-based lookups in VeDirectFrameHandler data.
 * Uses simple linear search - efficient for VE.Direct's small dataset (~20 fields max).
 * Zero heap allocation.
 */
class VeDirectHelper {
private:
    VeDirectFrameHandler* handler;

public:
    VeDirectHelper(VeDirectFrameHandler* veHandler) 
        : handler(veHandler) {
    }
    
    /**
     * Get value by name - returns pointer to value string or nullptr if not found
     * Call this after rxData() has processed incoming data
     */
    const char* getValue(const char* name) {
        for (int i = 0; i < handler->veEnd; i++) {
            if (strcmp(handler->veData[i].veName, name) == 0) {
                return handler->veData[i].veValue;
            }
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
        return getValue(name) != nullptr;
    }
};

#endif
