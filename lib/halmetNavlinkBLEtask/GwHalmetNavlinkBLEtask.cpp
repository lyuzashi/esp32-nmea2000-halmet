#include "GwHalmetNavlinkBLETask.h"
#include "GwApi.h"
#include <NimBLEDevice.h>

#include "navlink.cpp"
#include "GwChannel.h"
#include "GwChannelList.h"

#include "N2kMessages.h"

#define SERVICE_UUID        "ABF0"
#define CHARACTERISTIC_UUID "ABF2"
#define BLE_DEVICE_NAME     "NAVLinkBlue-1234"

// Server callback to handle connection/disconnection events
class ServerCallbacks: public NimBLEServerCallbacks {
    GwApi* api;
    GwChannel* channel;
public:
    ServerCallbacks(GwApi* api, GwChannel* channel) : api(api), channel(channel) {}
    
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
         if (api) api->getLogger()->logDebug(GwLog::LOG, "BLE connected");
         // Enable channel when client connects
         if (channel) channel->enable(true);
    }
    
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        if (api) api->getLogger()->logDebug(GwLog::LOG, "BLE disconnected");
        // Disable channel when no clients connected (pause processing)
        if (channel && pServer->getConnectedCount() == 0) {
            channel->enable(false);
        }
    } 
};

// Initialize BLE stack - returns server and characteristic
struct BLEContext {
    NimBLEServer* server;
    NimBLECharacteristic* characteristic;
};

BLEContext initializeBLE(GwApi* api) {
   
    if (api) api->getLogger()->logDebug(GwLog::LOG, "Init BLE");
    
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dbm

    // Disable security if not needed (saves ~2KB)
    NimBLEDevice::setSecurityAuth(false, false, false);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->advertiseOnDisconnect(true);
    
    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    NimBLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                                CHARACTERISTIC_UUID,
                                                NIMBLE_PROPERTY::READ | 
                                                NIMBLE_PROPERTY::WRITE |
                                                NIMBLE_PROPERTY::NOTIFY
                                                );
    
    pService->start();

    return {pServer, pCharacteristic};
}

void startAdvertising() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    
    NimBLEAdvertisementData advertisementData;
    advertisementData.setName(BLE_DEVICE_NAME);
    advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
    pAdvertising->setAdvertisementData(advertisementData);
    
    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(BLE_DEVICE_NAME);
    pAdvertising->setScanResponseData(scanResponseData);
    pAdvertising->start();
}


void navlinkBLETask(GwApi *api)
{
    // Wait for main setup to complete before accessing shared resources
    delay(500);
    
    BLEContext ble = initializeBLE(api);
    NimBLEServer* pServer = ble.server;
    NimBLECharacteristic* pCharacteristic = ble.characteristic;

    // Create NavLink channel
    NavLinkChannelImpl* navLinkImpl = new NavLinkChannelImpl([pServer, pCharacteristic](const char* msg, size_t len) {
        // Send message as BLE notification
        if (pCharacteristic && pServer->getConnectedCount() > 0) {
            pCharacteristic->setValue((uint8_t*)msg, len);
            pCharacteristic->notify();
        }
    });

    GwChannel *channel = new GwChannel(api->getLogger(), "NavLink", NAVLINK_CHANNEL_ID, -1);
    channel->setImpl(navLinkImpl);
    
    // NOTE: readActisense MUST be true for channelStream to be set!
    // Start disabled - will be enabled when BLE client connects
    channel->begin(false, false, false, "", "", false, false, true, true);
    
    // Set callbacks before starting advertising to avoid race condition
    ServerCallbacks* callbacks = new ServerCallbacks(api, channel);
    pServer->setCallbacks(callbacks);
    
    extern GwChannelList channels;
    channels.addChannel(channel);
    
    // Now safe to start advertising
    startAdvertising();
    api->getLogger()->logDebug(GwLog::LOG, "BLE initialized and advertising");

    // Keep task alive with minimal idle loop
    // NimBLE callbacks may need the originating task context
    while (true) {
        delay(10000);
    }
}


void navlinkBLEInit(GwApi *api)
{
    // When navlinkEnable, start the BLE task. If disabled, ensure BLE is off and resources are freed.
    bool enabled = api->getConfig()->getConfigItem(api->getConfig()->navlinkEnable, true)->asBoolean();
    if(enabled) {
        // NimBLE init needs substantial stack space
        api->addUserTask(navlinkBLETask, "navlinkBLETask", 4096);

     } else {
        // Release controller resources
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        // Free controller memory back to heap
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    }
}
