/**
 * NavLink BLE Channel for Halmet
 * 
 * Standalone GwChannel implementation for BLE N2K streaming.
 * Uses NavLink Blue (PDGY) format for bidirectional N2K message exchange.
 * 
 * Architecture:
 * - Implements GwChannelInterface, registered directly with GwChannelList
 * - Receives N2K via sendActisense() -> buffer -> ActisenseReader -> PDGY -> BLE notify
 * - Sends N2K via BLE write -> PDGY parse -> handleN2kMessage()
 * - No dedicated streaming task - main.cpp loop drives via channel interface
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
#include "GwChannelInterface.h"
#include "GwPdgyUtils.h"
#include "GwHalmetTask.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <NimBLEDevice.h>

extern GwChannelList channels;
extern void handleN2kMessage(const tN2kMsg &n2kMsg, int sourceId, bool isConverted);

#define BLE_CHANNEL_ID 252
#define BLE_BUFFER_SIZE 256

#define SERVICE_UUID        "ABF0"
#define CHARACTERISTIC_UUID "ABF2"
#define BLE_DEVICE_NAME     "NAVLinkBlue-1234"

// Queue for deferring BLE write processing (nimble_host stack is too small)
#define BLE_RX_QUEUE_SIZE 4
#define BLE_RX_MSG_SIZE 128  // Max PDGY message length

static QueueHandle_t g_bleRxQueue = nullptr;

static GwLog* g_logger = nullptr;
static GwChannel* g_bleChannel = nullptr;

// Forward declarations
class BLEChannelImpl;
static BLEChannelImpl* g_bleImpl = nullptr;

/**
 * Circular buffer for ActisenseReader input.
 */
class BLECircularBuffer : public Stream {
private:
    uint8_t buffer[BLE_BUFFER_SIZE];
    volatile int writePos = 0;
    volatile int readPos = 0;
    volatile int count = 0;
    
public:
    size_t write(uint8_t byte) override {
        if (count >= BLE_BUFFER_SIZE) {
            readPos = (readPos + 1) % BLE_BUFFER_SIZE;
            count--;
        }
        buffer[writePos] = byte;
        writePos = (writePos + 1) % BLE_BUFFER_SIZE;
        count++;
        return 1;
    }
    
    int available() override { return count; }
    
    int read() override { 
        if (count == 0) return -1;
        uint8_t b = buffer[readPos];
        readPos = (readPos + 1) % BLE_BUFFER_SIZE;
        count--;
        return b;
    }
    
    int peek() override { 
        return (count == 0) ? -1 : buffer[readPos];
    }
};

/**
 * BLE channel implementation.
 */
class BLEChannelImpl : public GwChannelInterface {
private:
    BLECircularBuffer* stream;
    tActisenseReader reader;
    NimBLEServer* pServer;
    NimBLECharacteristic* pCharacteristic;
    unsigned long messageCount = 0;
    
public:
    BLEChannelImpl() {
        stream = new BLECircularBuffer();
        reader.SetReadStream(stream);
        pServer = nullptr;
        pCharacteristic = nullptr;
    }
    
    virtual ~BLEChannelImpl() {
        delete stream;
    }
    
    void setServer(NimBLEServer* server) { pServer = server; }
    void setCharacteristic(NimBLECharacteristic* c) { pCharacteristic = c; }
    NimBLEServer* getServer() { return pServer; }
    
    bool hasConnectedClients() {
        return pServer && pServer->getConnectedCount() > 0;
    }
    
    // GwChannelInterface implementation
    virtual void loop(bool handleRead, bool handleWrite) override {
        // Process incoming queue FIRST (runs every main loop iteration)
        // This must happen regardless of client connection state
        if (g_bleRxQueue) {
            char msgBuf[BLE_RX_MSG_SIZE];
            while (xQueueReceive(g_bleRxQueue, msgBuf, 0) == pdTRUE) {
                if (g_logger) {
                    g_logger->logDebug(GwLog::LOG, "BLE loop: dequeued msg: %.40s", msgBuf);
                }
                processIncoming(msgBuf);
            }
        }
        
        if (!hasConnectedClients()) return;
        
        // Parse Actisense binary from buffer, convert to PDGY, send via BLE notify
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            char pdgyMsg[320];
            int len = n2kMsgToPdgy(msg, pdgyMsg, sizeof(pdgyMsg));
            if (len > 0 && pCharacteristic) {
                // Add CRLF for NavLink format
                pdgyMsg[len++] = '\r';
                pdgyMsg[len++] = '\n';
                pdgyMsg[len] = '\0';
                
                pCharacteristic->setValue((uint8_t*)pdgyMsg, len);
                pCharacteristic->notify();
            }
        }
    }
    
    virtual Stream* getStream(bool) override { return stream; }
    virtual void readMessages(GwMessageFetcher*) override {}
    virtual size_t sendToClients(const char*, int, bool) override { return 0; }
    
    /**
     * Process incoming PDGY message (called from micro-task with proper stack).
     * NOTE: This must NOT be called from BLE callback - use queueIncoming() instead.
     */
    bool processIncoming(const char* pdgyLine) {
        if (strncmp(pdgyLine, "!PDGY,", 6) != 0) {
            return false;
        }
        
        tN2kMsg msg;
        if (!pdgyToN2kMsg(pdgyLine, msg)) {
            if (g_logger) {
                g_logger->logDebug(GwLog::DEBUG, "BLE: PDGY parse failed: %.30s", pdgyLine);
            }
            return false;
        }
        
        // Inject into N2K system with our channel's sourceId
        if (g_logger) {
            g_logger->logDebug(GwLog::LOG, "BLE: injecting PGN %lu src %d via handleN2kMessage(sourceId=%d)", 
                              msg.PGN, msg.Source, BLE_CHANNEL_ID);
        }
        handleN2kMessage(msg, BLE_CHANNEL_ID, false);
        return true;
    }
};

// BLE characteristic write callback
// NOTE: Runs in nimble_host task with limited stack (~2KB)
// We must NOT do heavy processing here - just queue the message
class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0 && value.length() < BLE_RX_MSG_SIZE && g_bleRxQueue) {
            // Queue the message for processing by main loop
            char msgBuf[BLE_RX_MSG_SIZE];
            size_t len = value.length();
            memcpy(msgBuf, value.c_str(), len);
            if (g_logger) {
                g_logger->logDebug(GwLog::LOG, "BLE onWrite: len=%d data=%.40s", len, value.c_str());
            }
            
            // Strip trailing CR/LF
            while (len > 0 && (msgBuf[len-1] == '\r' || msgBuf[len-1] == '\n')) {
                len--;
            }
            msgBuf[len] = '\0';
            
            // Non-blocking queue send (don't wait if full)
            if (xQueueSend(g_bleRxQueue, msgBuf, 0) != pdTRUE) {
                if (g_logger) {
                    g_logger->logDebug(GwLog::DEBUG, "BLE: rx queue full, dropping message");
                }
            }
        }
    }
};

// BLE server callbacks
class ServerCallbacks: public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        if (g_logger) {
            g_logger->logDebug(GwLog::LOG, "BLE: connected, count=%d, heap=%d", 
                              pServer->getConnectedCount(), esp_get_free_heap_size());
        }
        
        // Enable channel when first client connects
        if (pServer->getConnectedCount() == 1 && g_bleChannel) {
            g_bleChannel->enable(true);
            if (g_logger) g_logger->logDebug(GwLog::LOG, "BLE: channel enabled");
        }
    }
    
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        if (g_logger) {
            g_logger->logDebug(GwLog::LOG, "BLE: disconnected (reason=%d), count=%d, heap=%d", 
                              reason, pServer->getConnectedCount(), esp_get_free_heap_size());
        }
        
        // Disable channel when no clients
        if (pServer->getConnectedCount() == 0 && g_bleChannel) {
            g_bleChannel->enable(false);
            if (g_logger) g_logger->logDebug(GwLog::LOG, "BLE: channel disabled");
        }
    }
};

static bool initializeBLE() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    if (!pServer) {
        if (g_logger) g_logger->logDebug(GwLog::ERROR, "BLE: createServer() failed");
        return false;
    }
    pServer->advertiseOnDisconnect(true);
    pServer->setCallbacks(new ServerCallbacks());
    g_bleImpl->setServer(pServer);
    
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    if (!pService) {
        if (g_logger) g_logger->logDebug(GwLog::ERROR, "BLE: createService() failed");
        return false;
    }
    
    NimBLECharacteristic* pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    g_bleImpl->setCharacteristic(pCharacteristic);
    
    pService->start();
    return true;
}

static void startAdvertising() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) {
        if (g_logger) g_logger->logDebug(GwLog::ERROR, "BLE: getAdvertising() returned null");
        return;
    }
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

void navlinkBLEInit(GwApi* api) {
    g_logger = api->getLogger();
    
    // Check if enabled
    bool enabled = api->getConfig()->getConfigItem(api->getConfig()->navlinkEnable, true)->asBoolean();
    if (!enabled) {
        g_logger->logDebug(GwLog::LOG, "BLE: disabled by config");
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        return;
    }
    
    g_logger->logDebug(GwLog::LOG, "BLE: init");
    
    // Create RX queue for deferred processing (BLE callback → micro-task)
    g_bleRxQueue = xQueueCreate(BLE_RX_QUEUE_SIZE, BLE_RX_MSG_SIZE);
    if (!g_bleRxQueue) {
        g_logger->logDebug(GwLog::ERROR, "BLE: failed to create rx queue");
        return;
    }
    
    // Create channel implementation
    g_bleImpl = new BLEChannelImpl();
    
    // Initialize BLE stack
    if (!initializeBLE()) {
        g_logger->logDebug(GwLog::ERROR, "BLE: init failed");
        delete g_bleImpl;
        g_bleImpl = nullptr;
        vQueueDelete(g_bleRxQueue);
        g_bleRxQueue = nullptr;
        return;
    }
    
    // Start advertising
    startAdvertising();
    g_logger->logDebug(GwLog::LOG, "BLE: initialized and advertising");
    
    // Create GwChannel wrapper
    g_bleChannel = new GwChannel(g_logger, "BLE", BLE_CHANNEL_ID, -1);
    g_bleChannel->setImpl(g_bleImpl);
    
    // Configure: start disabled, Actisense bidirectional
    g_bleChannel->begin(
        false,  // enabled - start disabled until clients connect
        false,  // NMEAin
        false,  // NMEAout
        "",     // readFilter
        "",     // writeFilter
        false,  // sendSeasmart
        false,  // toN2k
        true,   // readActisense
        true    // writeActisense
    );
    
    // Add to channel list
    channels.addChannel(g_bleChannel);
    g_logger->logDebug(GwLog::LOG, "BLE: channel added (ID=%d)", BLE_CHANNEL_ID);
    
    // Register cleanup micro-task with halmetTask (for advertising restart only)
    halmetRegisterMicroTask("BLE", []() {
        // Restart advertising if needed
        if (g_bleImpl && g_bleImpl->getServer()) {
            if (g_bleImpl->getServer()->getConnectedCount() == 0) {
                if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
                    if (g_logger) g_logger->logDebug(GwLog::LOG, "BLE: restarting advertising");
                    NimBLEDevice::getAdvertising()->start();
                }
            }
        }
    });
}

#endif  // NAVLINK_BLE_ENABLED
#endif  // BOARD_HALMET
