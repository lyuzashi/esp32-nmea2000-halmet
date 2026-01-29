#ifdef BOARD_HALMET
#ifdef NAVLINK_BLE_ENABLED
#include "GwNavlinkBLETask.h"
#include "GwApi.h"
#include <WiFi.h>
#include <esp_wifi.h>

#include <NimBLEDevice.h>


// #include "GwChannelInterface.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "N2kMessages.h"

#include "navlink.cpp"

#define SERVICE_UUID        "ABF0"
#define CHARACTERISTIC_UUID "ABF2"
#define BLE_DEVICE_NAME     "NAVLinkBlue-1234"

static GwApi* g_api = nullptr;
static NimBLEServer *g_pServer = nullptr;
static NimBLEAdvertising *g_pAdvertising = nullptr;
static NimBLECharacteristic *g_pCharacteristic = nullptr;

// Radio state machine
enum RadioState {
    BOTH_STANDBY,   // Both radios advertising, minimal resources
    WIFI_ACTIVE,    // WiFi has connection, BLE stopped
    BLE_ACTIVE      // BLE has connection, WiFi in minimal mode
};

static RadioState g_radioState = BOTH_STANDBY;
static unsigned long g_lastStateChange = 0;
static bool g_bleInitialized = false;
static unsigned long g_lastWifiActivity = 0;  // Track actual web activity
#define WIFI_ACTIVITY_TIMEOUT 5000  // Consider WiFi inactive after 5s of no activity

// Forward declaration for BLE initialization
void initializeBLE(GwApi* api);

// Transition to new state with resource management
void transitionToState(RadioState newState) {
    if (newState == g_radioState) return;
    
    RadioState oldState = g_radioState;
    g_radioState = newState;
    g_lastStateChange = millis();
    
    size_t heapBefore = xPortGetFreeHeapSize();
    
    switch (newState) {
        case BOTH_STANDBY:
            if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "State: BOTH_STANDBY");
            // Return both radios to minimal standby mode
            if (oldState == WIFI_ACTIVE) {
                // Reinitialize BLE if it was deinitialized
                if (!g_bleInitialized) {
                    if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Reinitializing BLE...");
                    delay(100); // Reduced delay
                    initializeBLE(g_api);
                } else {
                    // Just restart advertising if still initialized
                    if (g_pAdvertising && !g_pAdvertising->isAdvertising()) {
                        g_pAdvertising->start();
                    }
                }
                
                // If in AP mode, disconnect clients (but keep AP active)
                // If in client mode, WiFi stays connected (just freed BLE resources)
                if (WiFi.softAPgetStationNum() > 0) {
                    WiFi.softAPdisconnect(false);  // Disconnect AP clients, keep AP
                }
            } else if (oldState == BLE_ACTIVE) {
                // Restore WiFi AP if it was disabled during BLE operation
                // The AP config is preserved, just need to re-enable
                if (!WiFi.softAPgetStationNum() && WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
                    if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Re-enabling WiFi AP after BLE session");
                    // Restart AP with existing configuration
                    WiFi.mode(WIFI_AP_STA);  // Enable AP mode again
                    delay(100);
                    
                    // Configure AP to detect disconnected stations quickly (default is 300 seconds)
                    esp_err_t err = esp_wifi_set_inactive_time(WIFI_IF_AP, 10);  // 10 seconds
                    if (err) {
                        g_api->getLogger()->logDebug(GwLog::ERROR, "Failed to set AP inactive timeout: %d", (int)err);
                    } else {
                        g_api->getLogger()->logDebug(GwLog::DEBUG, "AP inactive timeout set to 10 seconds");
                    }
                }
            }
            break;
            
        case WIFI_ACTIVE:
            if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "State: WIFI_ACTIVE - deinit BLE");
            // Completely deinitialize BLE to free ALL memory (~15-20KB)
            if (g_bleInitialized) {
                // Step 1: Detach from NavLink to prevent any access during shutdown
                extern NavLinkChannelImpl *navLinkImpl;
                if (navLinkImpl != nullptr) {
                    navLinkImpl->setBleCharacteristic(nullptr);
                }
                delay(20);
                
                // Step 2: Stop advertising first
                if (g_pAdvertising && g_pAdvertising->isAdvertising()) {
                    if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Stop BLE adv");
                    g_pAdvertising->stop();
                    delay(50);
                }
                
                // Step 3: Disconnect all clients gracefully
                if (g_pServer) {
                    int clientCount = g_pServer->getConnectedCount();
                    if (clientCount > 0) {
                        if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Disc %d BLE clients", clientCount);
                        for (int i = clientCount - 1; i >= 0; i--) {
                            g_pServer->disconnect(i);
                        }
                        delay(100); // Allow disconnections to complete
                    }
                }
                
                // Step 4: Stop the server before deinit
                if (g_pServer) {
                    if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Stop BLE server");
                    // Remove callbacks to prevent dangling pointer access
                    g_pServer->setCallbacks(nullptr);
                    delay(20);
                }
                
                // Step 5: Deinitialize NimBLE stack - DON'T clear pointers first!
                // NimBLE needs to access these objects during cleanup
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "NimBLE deinit");
                
                NimBLEDevice::deinit(true);  // true = clear all
                
                // Step 6: NOW clear pointers after successful deinit
                delay(100);
                g_pServer = nullptr;
                g_pAdvertising = nullptr;
                g_pCharacteristic = nullptr;
                
                g_bleInitialized = false;
                
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "BLE deinit done");
            }
            break;
            
        case BLE_ACTIVE:
            if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "State: BLE_ACTIVE - WiFi min");
            // Completely shut down WiFi to prevent connections while BLE is active
            // This prevents simultaneous WiFi+BLE which causes heap exhaustion
            
            // Disconnect any active clients first
            if (WiFi.softAPgetStationNum() > 0) {
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Disc %d WiFi AP", WiFi.softAPgetStationNum());
                WiFi.softAPdisconnect(true);  // true = disable AP
                delay(50);
            }
            
            // If in client mode, disconnect but don't fully disable
            // (allow quick reconnection when BLE disconnects)
            if (WiFi.status() == WL_CONNECTED) {
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Disc WiFi client");
                WiFi.disconnect(false);  // false = don't disable WiFi, just disconnect
                delay(50);
            }
            break;
    }
    
    size_t heapAfter = xPortGetFreeHeapSize();
    if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Heap: %u (%+d)", 
                                            heapAfter, (int)(heapAfter - heapBefore));
}

// Check connection states and manage transitions
void updateRadioState() {
    // Check both WiFi modes:
    // 1. AP mode: ESP32 is an access point, check for connected stations
    // 2. Client mode: ESP32 is connected to a router/SSID, assume web access is possible
    bool apHasClients = (WiFi.softAPgetStationNum() > 0);
    bool clientModeConnected = (WiFi.status() == WL_CONNECTED);
    
    // WiFi is "active" if either:
    // - AP mode has clients connected, OR
    // - Client mode is connected (someone could be accessing via network)
    bool wifiHasActivity = apHasClients || clientModeConnected;
    
    bool bleHasClients = g_pServer && (g_pServer->getConnectedCount() > 0);
    
    // Additional check: if in WIFI_ACTIVE, track activity
    if (g_radioState == WIFI_ACTIVE && wifiHasActivity) {
        // Update activity timestamp when WiFi is in use
        g_lastWifiActivity = millis();
    }
    
    // Consider WiFi inactive if:
    // - No AP clients AND not in client mode, OR
    // - No activity for timeout period (protects against idle connections)
    bool wifiReallyActive = wifiHasActivity && 
                           ((millis() - g_lastWifiActivity) < WIFI_ACTIVITY_TIMEOUT);
    
    switch (g_radioState) {
        case BOTH_STANDBY:
            // Transition to active state when WiFi becomes usable
            if (wifiHasActivity) {
                g_lastWifiActivity = millis();
                if (g_api && apHasClients) {
                    g_api->getLogger()->logDebug(GwLog::LOG, "WiFi AP: %d clients", WiFi.softAPgetStationNum());
                }
                transitionToState(WIFI_ACTIVE);
            } else if (bleHasClients) {
                transitionToState(BLE_ACTIVE);
            }
            // Initialize BLE lazily only when staying in BOTH_STANDBY
            // This avoids initializing BLE if WiFi connects immediately at boot
            else if (!g_bleInitialized && !wifiHasActivity) {
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "Lazy init BLE (no WiFi)");
                initializeBLE(g_api);
            }
            break;
            
        case WIFI_ACTIVE:
            // Return to standby when WiFi clients disconnect AND activity timeout
            if (!wifiReallyActive) {
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "WiFi inactive");
                transitionToState(BOTH_STANDBY);
            }
            break;
            
        case BLE_ACTIVE:
            // If WiFi activity detected, prioritize WiFi over BLE
            // This handles case where user tries to connect via WiFi while BLE is active
            if (apHasClients || clientModeConnected) {
                if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "WiFi detected, switch");
                g_lastWifiActivity = millis();
                transitionToState(WIFI_ACTIVE);
            }
            // Return to standby when BLE clients disconnect
            else if (!bleHasClients) {
                transitionToState(BOTH_STANDBY);
            }
            break;
    }
}

// Server callback to handle connection/disconnection events
class ServerCallbacks: public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
         if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "BLE connected");
         // Trigger state transition on next loop
    }
    
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        if (g_api) g_api->getLogger()->logDebug(GwLog::LOG, "BLE disconnected");
        // Trigger state transition on next loop
    } 
};

static ServerCallbacks serverCallbacks;

// Initialize BLE stack
void initializeBLE(GwApi* api) {
    if (g_bleInitialized) return;
    
    if (api) api->getLogger()->logDebug(GwLog::LOG, "Init BLE");
    size_t heapBefore = xPortGetFreeHeapSize();
    
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dbm
    
    g_pServer = NimBLEDevice::createServer();
    g_pServer->setCallbacks(&serverCallbacks);
    g_pServer->advertiseOnDisconnect(true);
    
    NimBLEService *pService = g_pServer->createService(SERVICE_UUID);
    g_pCharacteristic = pService->createCharacteristic(
                                                CHARACTERISTIC_UUID,
                                                NIMBLE_PROPERTY::READ | 
                                                NIMBLE_PROPERTY::WRITE |
                                                NIMBLE_PROPERTY::NOTIFY
                                                );
    
    pService->start();
    
    // Attach BLE characteristic to NavLink channel
    extern NavLinkChannelImpl *navLinkImpl;
    if (navLinkImpl != nullptr) {
        navLinkImpl->setBleCharacteristic(g_pCharacteristic);
    }
    
    // Configure and start advertising
    g_pAdvertising = NimBLEDevice::getAdvertising();
    g_pAdvertising->addServiceUUID(SERVICE_UUID);
    
    NimBLEAdvertisementData advertisementData;
    advertisementData.setName(BLE_DEVICE_NAME);
    advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
    g_pAdvertising->setAdvertisementData(advertisementData);
    
    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(BLE_DEVICE_NAME);
    g_pAdvertising->setScanResponseData(scanResponseData);
    g_pAdvertising->start();
    
    g_bleInitialized = true;
    
    size_t heapAfter = xPortGetFreeHeapSize();
    if (api) api->getLogger()->logDebug(GwLog::LOG, "BLE init: -%d heap=%u", 
                                        (int)(heapBefore - heapAfter), heapAfter);
}


void navlinkBLETask(GwApi *api)
{
    g_api = api;
    
    // Wait for WiFi to initialize
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 50)
    {
        delay(100);
        retries++;
    }

    api->getLogger()->logDebug(GwLog::LOG, "Dual-radio init, heap: %u", xPortGetFreeHeapSize());
    
    // Configure WiFi AP to detect disconnected stations quickly
    // Default is 300 seconds (5 minutes), set to 10 seconds for faster state transitions
    esp_err_t err = esp_wifi_set_inactive_time(WIFI_IF_AP, 10);
    if (err) {
        api->getLogger()->logDebug(GwLog::ERROR, "Failed to set AP inactive timeout: %d", (int)err);
    } else {
        api->getLogger()->logDebug(GwLog::DEBUG, "AP inactive timeout set to 10 seconds");
    }
    
    // Don't initialize BLE at boot - let state machine do it only when needed
    // If WiFi is active, BLE will never initialize (saves heap)
    // If no WiFi activity, BLE will initialize on first transition to BOTH_STANDBY
    api->getLogger()->logDebug(GwLog::LOG, "Boot complete, heap: %u", xPortGetFreeHeapSize());

    // Main loop with state machine
    static int counter = 0;
    static tN2kMsg testMsg2;  // Allocate once, make static
    static size_t minHeap = xPortGetFreeHeapSize();
    
    while (true)
    {
        delay(2000); // Check every 2 seconds
        counter++;
        
        // Update state machine based on connection status
        updateRadioState();
        
        size_t currentHeap = xPortGetFreeHeapSize();
        if (currentHeap < minHeap) minHeap = currentHeap;
        
        // Detailed status every 15 iterations (30 seconds)
        if (counter % 15 == 0) {
            static const char* stateNames[] = {"STANDBY", "WIFI", "BLE"};
            
            api->getLogger()->logDebug(GwLog::LOG, 
                "%s W:%d B:%d H:%u (min:%u)", 
                stateNames[g_radioState],
                WiFi.softAPgetStationNum(),
                (g_pServer ? g_pServer->getConnectedCount() : 0),
                currentHeap, 
                minHeap);
            
            // Send test NMEA2000 message
            SetN2kTemperatureExt(testMsg2, 0, 1, N2kts_SeaTemperature, CToKelvin(25.5));
            api->sendN2kMessage(testMsg2);
        }
    }
    vTaskDelete(NULL);
}

NavLinkChannelImpl *navLinkImpl = nullptr;

void navlinkBLEInit(GwApi *api)
{
    // Stack size reduced to 5000 - optimized for BLE operations
    api->addUserTask(navlinkBLETask, "navlinkBLETask", 6000);

    GwLog *logger = api->getLogger();

    // Create NavLink channel
    navLinkImpl = new NavLinkChannelImpl(logger);
    GwChannel *channel = new GwChannel(logger, "NavLink", NAVLINK_CHANNEL_ID, -1);
    channel->setImpl(navLinkImpl);
    
    // NOTE: readActisense MUST be true for channelStream to be set!
    channel->begin(true, false, false, "", "", false, false, true, true);
    
    extern GwChannelList channels;
    channels.addChannel(channel);



}
#endif // BOARD_HALMET
#endif // NAVLINK_BLE_ENABLED