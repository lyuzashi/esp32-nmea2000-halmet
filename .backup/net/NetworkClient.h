// Compatibility shim for NetworkClient.h
// Arduino-ESP32 3.x moved NetworkClient functionality
#ifndef NETWORK_CLIENT_H_COMPAT
#define NETWORK_CLIENT_H_COMPAT

#include <Arduino.h>
#include <Client.h>

// WiFiClient inherits from Client which provides the base functionality
// This stub satisfies the #include requirement

#endif // NETWORK_CLIENT_H_COMPAT
