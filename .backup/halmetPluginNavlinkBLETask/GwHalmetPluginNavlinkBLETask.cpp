// # ifdef BOARD_HALMET
// # ifdef NAVLINK_BLE_ENABLED
#include "GwHalmetPluginNavlinkBLETask.h"
#include "GwApi.h"
// #include <WiFi.h>
// #include <esp_wifi.h>
// #include <Preferences.h>

#include <NimBLEDevice.h>


// #include "GwChannelInterface.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "N2kMessages.h"

// #include "navlink.cpp"

#define SERVICE_UUID        "ABF0"
#define CHARACTERISTIC_UUID "ABF2"
#define BLE_DEVICE_NAME     "NAVLinkBlue-1234"

static GwApi* g_api = nullptr;
static bool g_bleModeEnabled = true;  // BLE enabled by default
static NimBLEServer *g_pServer = nullptr;
static NimBLEAdvertising *g_pAdvertising = nullptr;
static NimBLECharacteristic *g_pCharacteristic = nullptr;

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
   
    if (api) api->getLogger()->logDebug(GwLog::LOG, "Init BLE");
    
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
    // if (navLinkImpl != nullptr) {
    //     navLinkImpl->setBleCharacteristic(g_pCharacteristic);
    // }
    
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
    

}


void navlinkBLETask(GwApi *api)
{
    g_api = api;

    initializeBLE(g_api);

    while (true)
    {
        delay(2000);
       
    }
    vTaskDelete(NULL);
}


void navlinkBLEInit(GwApi *api)
{
    GwLog *logger = api->getLogger();
    logger->logDebug(GwLog::LOG, "=== NavLink Init ===");
    
    api->addUserTask(navlinkBLETask, "navlinkBLETask", 6000);

  
}
// #endif // BOARD_HALMET
// #endif // NAVLINK_BLE_ENABLED