/**
 * NavLink BLE Channel for Halmet
 * 
 * Lightweight N2K message streaming over BLE.
 * Uses NavLink Blue (PDGY) format for bidirectional message exchange.
 * 
 * Architecture:
 * - Uses beginBidirectional() - channel manages queues internally
 * - Transport only handles: BLE connection, formatting, sending
 * - Output: channel queues msg -> loop formats to PDGY -> BLE notify
 * - Input: BLE write -> channel->queueIncoming() -> loop parse -> handleN2kMessage()
 * 
 * BLE Service: ABF0 (NavLink Blue)
 * Characteristic: ABF2 (read/write/notify)
 */
#include "GwHalmetNavlinkBLETask.h"

#ifdef BOARD_HALMET
#ifdef NAVLINK_BLE_ENABLED

#include "GwApi.h"
#include "GwLog.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "GwPdgyUtils.h"
#include "GwHalmetTask.h"
#include "N2kMsg.h"
#include <NimBLEDevice.h>
#include <esp_mac.h>

extern GwChannelList channels;

#define BLE_CHANNEL_ID 252

#define SERVICE_UUID        "ABF0"
#define CHARACTERISTIC_UUID "ABF2"
#define BLE_DEVICE_NAME_PREFIX "NAVLinkBlue-"

static char g_bleDeviceName[20];  // "NAVLinkBlue-XXXX" + null
static GwLog* g_logger = nullptr;
static GwChannel* g_bleChannel = nullptr;
static NimBLEServer* g_bleServer = nullptr;
static NimBLECharacteristic* g_bleCharacteristic = nullptr;

//=============================================================================
// Transport functions (provided to channel)
//=============================================================================

/**
 * Format N2K message to PDGY string with CRLF.
 */
static int formatPdgy(const tN2kMsg& msg, char* buf, size_t bufLen) {
    int len = n2kMsgToPdgy(msg, buf, bufLen - 3);
    if (len > 0) {
        buf[len++] = '\r';
        buf[len++] = '\n';
        buf[len] = '\0';
    }
    return len;
}

/**
 * Parse PDGY string to N2K message.
 */
static bool parsePdgy(const char* line, tN2kMsg& msg) {
    return pdgyToN2kMsg(line, msg);
}

/**
 * Send formatted data via BLE notify.
 */
static bool sendBle(const char* data, size_t len) {
    if (!g_bleCharacteristic || !data || len < 7) return false;  // Min PDGY: "!PDGY,x"
    g_bleCharacteristic->setValue((uint8_t*)data, len);
    g_bleCharacteristic->notify();
    return true;
}

/**
 * Check if BLE has connected clients.
 */
static bool isBleConnected() {
    return g_bleServer && g_bleServer->getConnectedCount() > 0;
}

//=============================================================================
// BLE callbacks
//=============================================================================

class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0 && g_bleChannel) {
            // Queue incoming data - channel handles the rest
            if (!g_bleChannel->queueIncoming(value.c_str(), value.length())) {
                if (g_logger) g_logger->logDebug(GwLog::DEBUG, "BLE: rx queue full");
            }
        }
    }
};

class ServerCallbacks: public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        if (g_logger) {
            g_logger->logDebug(GwLog::LOG, "BLE: connected, count=%d", pServer->getConnectedCount());
        }
        if (pServer->getConnectedCount() == 1 && g_bleChannel) {
            g_bleChannel->enable(true);
        }
    }
    
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        if (g_logger) {
            g_logger->logDebug(GwLog::LOG, "BLE: disconnected, count=%d", pServer->getConnectedCount());
        }
        if (pServer->getConnectedCount() == 0 && g_bleChannel) {
            g_bleChannel->enable(false);
        }
    }
};

//=============================================================================
// BLE initialization
//=============================================================================

static bool initializeBLE() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false);

    g_bleServer = NimBLEDevice::createServer();
    if (!g_bleServer) {
        if (g_logger) g_logger->logDebug(GwLog::ERROR, "BLE: createServer() failed");
        return false;
    }
    g_bleServer->advertiseOnDisconnect(true);
    g_bleServer->setCallbacks(new ServerCallbacks());
    
    NimBLEService* pService = g_bleServer->createService(SERVICE_UUID);
    if (!pService) {
        if (g_logger) g_logger->logDebug(GwLog::ERROR, "BLE: createService() failed");
        return false;
    }
    
    g_bleCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    g_bleCharacteristic->setCallbacks(new CharacteristicCallbacks());
    
    return true;
}

static void startAdvertising() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) return;
    
    pAdvertising->addServiceUUID(SERVICE_UUID);
    
    NimBLEAdvertisementData advertisementData;
    advertisementData.setName(g_bleDeviceName);
    advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
    pAdvertising->setAdvertisementData(advertisementData);
    
    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(g_bleDeviceName);
    pAdvertising->setScanResponseData(scanResponseData);
    pAdvertising->start();
}

//=============================================================================
// Microtask: advertising restart only (queues handled by channel loop)
//=============================================================================

static void bleMicroTask(GwApi* api) {
    if (g_bleServer && g_bleServer->getConnectedCount() == 0) {
        if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
            if (g_logger) g_logger->logDebug(GwLog::LOG, "BLE: restarting advertising");
            NimBLEDevice::getAdvertising()->start();
        }
    }
}

//=============================================================================
// Init
//=============================================================================

void navlinkBLEInit(GwApi* api) {
    g_logger = api->getLogger();
    
    // Generate unique device name from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_bleDeviceName, sizeof(g_bleDeviceName), "%s%02X%02X", 
             BLE_DEVICE_NAME_PREFIX, mac[4], mac[5]);
    
    bool enabled = api->getConfig()->getConfigItem(api->getConfig()->navlinkEnable, true)->asBoolean();
    if (!enabled) {
        g_logger->logDebug(GwLog::LOG, "BLE: disabled by config");
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        return;
    }
    
    g_logger->logDebug(GwLog::LOG, "BLE: init");
    
    if (!initializeBLE()) {
        g_logger->logDebug(GwLog::ERROR, "BLE: init failed");
        return;
    }
    
    startAdvertising();
    g_logger->logDebug(GwLog::LOG, "BLE: advertising as '%s'", g_bleDeviceName);
    
    // Create bidirectional channel - queues managed by channel
    g_bleChannel = new GwChannel(g_logger, "BLE", BLE_CHANNEL_ID, -1);
    if (!g_bleChannel->beginBidirectional(
            formatPdgy,      // formatter: N2K -> PDGY
            parsePdgy,       // parser: PDGY -> N2K
            sendBle,         // sender: PDGY -> BLE notify
            isBleConnected,  // connection check
            8,               // TX queue size (reduced from 16)
            4,               // RX queue size
            128              // RX message size
        )) {
        g_logger->logDebug(GwLog::ERROR, "BLE: failed to create channel");
        return;
    }
    
    // Start disabled until clients connect
    g_bleChannel->enable(false);
    
    channels.addChannel(g_bleChannel);
    g_logger->logDebug(GwLog::LOG, "BLE: bidirectional channel added (ID=%d)", BLE_CHANNEL_ID);
    
    // Microtask only handles advertising restart now
    halmetRegisterMicroTask("BLE", bleMicroTask);
}

#endif  // NAVLINK_BLE_ENABLED
#endif  // BOARD_HALMET
