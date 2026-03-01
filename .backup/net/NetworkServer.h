// Compatibility shim for NetworkServer.h
// Arduino-ESP32 3.x moved NetworkServer functionality
#ifndef NETWORK_SERVER_H_COMPAT
#define NETWORK_SERVER_H_COMPAT

#include <Arduino.h>
#include <Server.h>

// WiFiServer inherits from Server which provides the base functionality
// This stub satisfies the #include requirement

#endif // NETWORK_SERVER_H_COMPAT
