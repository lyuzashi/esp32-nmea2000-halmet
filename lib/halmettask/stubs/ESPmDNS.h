#ifndef _ESPMDNS_STUB_H
#define _ESPMDNS_STUB_H

#include <Arduino.h>
#include <esp_err.h>
#include <esp_netif_ip_addr.h>

// Stub MDNS implementation - no-op to save ~5-6KB heap
class MDNSResponder {
public:
    bool begin(const char* hostName) { return true; }
    void addService(const char *service, const char *proto, uint16_t port) {}
    void end() {}
};

extern MDNSResponder MDNS;

// Stub for MDNS query function - always return error (no resolution)
inline esp_err_t mdns_query_a(const char *host, uint32_t timeout, esp_ip4_addr_t *addr) {
    return ESP_FAIL;
}

#endif
