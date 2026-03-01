// Compatibility shim for NetworkUdp.h
// Arduino-ESP32 3.x moved NetworkUdp functionality
#ifndef NETWORK_UDP_H_COMPAT
#define NETWORK_UDP_H_COMPAT

#include <Arduino.h>
#include <Udp.h>

// WiFiUDP inherits from UDP which provides the base functionality
// This stub satisfies the #include requirement

#endif // NETWORK_UDP_H_COMPAT
