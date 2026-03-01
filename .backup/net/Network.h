// Compatibility shim for Network.h
// Arduino-ESP32 3.x moved Network functionality to WiFiGeneric
#ifndef NETWORK_H_COMPAT
#define NETWORK_H_COMPAT

#include <Arduino.h>

// Stub to satisfy #include "Network.h" requirement
// Actual network functionality is in WiFiGeneric.h

#endif // NETWORK_H_COMPAT
