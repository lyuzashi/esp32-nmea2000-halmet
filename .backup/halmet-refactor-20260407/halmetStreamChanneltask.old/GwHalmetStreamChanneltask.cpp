/**
 * Halmet Stream Channel
 * 
 * Shared channel for streaming NMEA2000 messages in NavLink Blue format.
 * Multiple listeners (BLE, WebSocket, etc.) can register callbacks.
 */
#include "GwHalmetStreamChanneltask.h"

#ifdef BOARD_HALMET

#include "GwApi.h"
#include "GwLog.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "GwChannelInterface.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <base64.h>           // encode only
#include <mbedtls/base64.h>  // decode
#include <vector>

extern GwChannelList channels;

// External declaration for direct N2K message injection with sourceId control
// This allows us to use our channel's sourceId (250) so GwChannel::sendActisense()
// skips our channel, preventing echo loops.
extern void handleN2kMessage(const tN2kMsg &n2kMsg, int sourceId, bool isConverted);

#define STREAM_CHANNEL_ID 250
#define STREAM_BUFFER_SIZE 256

static GwLog* g_logger = nullptr;
static GwChannel* g_streamChannel = nullptr;

// Listener entry with name
struct ListenerEntry {
    HalmetStreamHandle handle;
    String name;
    HalmetStreamCallback callback;
    bool active;
};

// Circular buffer for ActisenseReader
class StreamCircularBuffer : public Stream {
private:
    uint8_t buffer[STREAM_BUFFER_SIZE];
    volatile int writePos = 0;
    volatile int readPos = 0;
    volatile int count = 0;
    
public:
    size_t write(uint8_t byte) override {
        if (count >= STREAM_BUFFER_SIZE) {
            readPos = (readPos + 1) % STREAM_BUFFER_SIZE;
            count--;
        }
        buffer[writePos] = byte;
        writePos = (writePos + 1) % STREAM_BUFFER_SIZE;
        count++;
        return 1;
    }
    
    int available() override { return count; }
    
    int read() override { 
        if (count == 0) return -1;
        uint8_t b = buffer[readPos];
        readPos = (readPos + 1) % STREAM_BUFFER_SIZE;
        count--;
        return b;
    }
    
    int peek() override { 
        return (count == 0) ? -1 : buffer[readPos];
    }
};

// Shared channel implementation with listener management
class HalmetStreamChannelImpl : public GwChannelInterface, public HalmetStreamInterface {
private:
    StreamCircularBuffer* stream;
    tActisenseReader reader;
    std::vector<ListenerEntry> listeners;
    std::vector<HalmetCleanupCallback> cleanupCallbacks;
    SemaphoreHandle_t listenerLock;
    HalmetStreamHandle nextHandle = 1;
    unsigned long messageCount = 0;
    GwApi* api = nullptr;
    
    // Counter IDs for outgoing and incoming messages
    // Outgoing: "Stream" counter with per-listener sub-keys (WS, BLE)
    // Incoming: "StreamRx" counter with per-listener sub-keys
    int counterIdOut = -1;
    int counterIdIn = -1;
    
public:
    HalmetStreamChannelImpl() {
        stream = new StreamCircularBuffer();
        reader.SetReadStream(stream);
        listenerLock = xSemaphoreCreateMutex();
    }
    
    virtual ~HalmetStreamChannelImpl() {
        delete stream;
        vSemaphoreDelete(listenerLock);
    }
    
    // Set the API and counter IDs for immediate counting
    void setCounters(GwApi* a, int outId, int inId) {
        api = a;
        counterIdOut = outId;
        counterIdIn = inId;
    }
    
    // GwChannelInterface implementation
    virtual void loop(bool handleRead, bool handleWrite) override {
        size_t activeListeners = listenerCount();
        if (activeListeners == 0) return;
        
        // Parse messages and dispatch to all listeners
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            // Format: !PDGY,<pgn>,<prio>,<src>,<dst>,<timer>,<base64data>
            float timer = (millis() % 100000000) / 1000.0f;
            String encoded = base64::encode(msg.Data, msg.DataLen);
            
            char pdgyMsg[256];
            int len = snprintf(pdgyMsg, sizeof(pdgyMsg), 
                "!PDGY,%lu,%d,%d,%d,%.3f,%s",
                msg.PGN, msg.Priority, msg.Source, msg.Destination, 
                timer, encoded.c_str());
            
            // Dispatch to all active listeners
            dispatchToListeners(pdgyMsg, len);
        }
    }
    
    virtual Stream* getStream(bool) override { return stream; }
    virtual void readMessages(GwMessageFetcher*) override {}
    virtual size_t sendToClients(const char*, int, bool) override { return 0; }
    
    // HalmetStreamInterface implementation
    virtual HalmetStreamHandle addListener(const char* name, HalmetStreamCallback callback) override {
        xSemaphoreTake(listenerLock, portMAX_DELAY);
        
        HalmetStreamHandle handle = nextHandle++;
        listeners.push_back({handle, String(name), callback, true});
        
        size_t count = countActiveListeners();
        xSemaphoreGive(listenerLock);
        
        // Enable channel when first listener added
        if (count == 1 && g_streamChannel) {
            g_streamChannel->enable(true);
            if (g_logger) {
                g_logger->logDebug(GwLog::LOG, "HalmetStream: channel enabled (%s)", name);
            }
        }
        
        return handle;
    }
    
    virtual void removeListener(HalmetStreamHandle handle) override {
        xSemaphoreTake(listenerLock, portMAX_DELAY);
        
        for (auto& entry : listeners) {
            if (entry.handle == handle) {
                entry.active = false;
                break;
            }
        }
        
        size_t count = countActiveListeners();
        xSemaphoreGive(listenerLock);
        
        // Disable channel when no listeners
        if (count == 0 && g_streamChannel) {
            g_streamChannel->enable(false);
            if (g_logger) {
                g_logger->logDebug(GwLog::LOG, "HalmetStream: channel disabled (no listeners)");
            }
        }
    }
    
    virtual size_t listenerCount() const override {
        xSemaphoreTake(listenerLock, portMAX_DELAY);
        size_t count = countActiveListeners();
        xSemaphoreGive(listenerLock);
        return count;
    }
    
    virtual void addCleanupCallback(HalmetCleanupCallback callback) override {
        cleanupCallbacks.push_back(callback);
    }
    
    /**
     * Process incoming PDGY message from a listener (WS or BLE client).
     * 
     * ECHO PREVENTION STRATEGY:
     * 
     * Problem: When WS client A sends a message, we don't want:
     *   - Client A to receive its own message back (direct echo)
     *   - Client A to receive it again via N2K distribution (indirect echo)
     * 
     * Solution (two-phase dispatch):
     * 
     * Phase 1: Direct distribution to OTHER stream listeners
     *   - We call dispatchToListenersExcept(fromHandle) to send immediately
     *     to all listeners EXCEPT the sender
     *   - This ensures other WS/BLE clients get the message quickly
     * 
     * Phase 2: N2K system injection with sourceId trick
     *   - We call handleN2kMessage() with STREAM_CHANNEL_ID (250) as sourceId
     *   - In main.cpp, this distributes to all channels via sendActisense()
     *   - GwChannel::sendActisense() checks: if (this->sourceId == sourceId) return;
     *   - Our channel has sourceId=250, so it SKIPS us entirely
     *   - Result: N2K bus + other channels get it, but NOT our listeners again
     * 
     * Why not use api->sendN2kMessage()?
     *   - api->sendN2kMessage() uses MIN_USER_TASK as sourceId (not 250)
     *   - This would NOT skip our channel, causing duplicate delivery
     *   - By calling handleN2kMessage directly with our sourceId, we get proper skip
     */
    virtual bool processIncoming(const char* listenerName, const char* pdgyLine, HalmetStreamHandle fromHandle) override {
        if (!api || !pdgyLine) return false;
        
        // Parse: !PDGY,<pgn>,<prio>,<src>,<dst>,<timer>,<base64data>
        // Example: !PDGY,127250,2,22,255,12345.678,AAABBCC==
        
        // Skip "!PDGY," prefix
        if (strncmp(pdgyLine, "!PDGY,", 6) != 0) {
            if (g_logger) g_logger->logDebug(GwLog::DEBUG, "StreamRx: invalid prefix");
            return false;
        }
        const char* p = pdgyLine + 6;
        
        // Parse comma-separated fields
        unsigned long pgn;
        int priority, source, dest;
        float timer;
        char base64Data[256];
        
        // Use sscanf for robust parsing
        int parsed = sscanf(p, "%lu,%d,%d,%d,%f,%255[^\r\n]",
                           &pgn, &priority, &source, &dest, &timer, base64Data);
        
        if (parsed < 6) {
            if (g_logger) g_logger->logDebug(GwLog::DEBUG, "StreamRx: parse failed (%d fields)", parsed);
            return false;
        }
        
        // Base64 decode the data payload using mbedtls
        // (Arduino base64.h only has encode, not decode)
        uint8_t decodedBuf[224];  // Max N2K data is 223 bytes
        size_t decodedLen = 0;
        int ret = mbedtls_base64_decode(decodedBuf, sizeof(decodedBuf), &decodedLen,
                                        (const unsigned char*)base64Data, strlen(base64Data));
        if (ret != 0 || decodedLen == 0 || decodedLen > 223) {
            if (g_logger) g_logger->logDebug(GwLog::DEBUG, "StreamRx: base64 decode failed ret=%d len=%d", ret, decodedLen);
            return false;
        }
        
        // Construct tN2kMsg
        tN2kMsg msg;
        msg.Init(priority, pgn, source, dest);
        for (size_t i = 0; i < decodedLen; i++) {
            msg.AddByte(decodedBuf[i]);
        }
        
        // PHASE 1: Distribute to OTHER stream listeners (excluding sender)
        // Format back to PDGY for distribution (we already have pdgyLine but may need CRLF)
        dispatchToListenersExcept(pdgyLine, strlen(pdgyLine), fromHandle);
        
        // PHASE 2: Inject into N2K system with OUR channel's sourceId
        // Using STREAM_CHANNEL_ID (250) as sourceId causes GwChannel::sendActisense()
        // to skip our channel (sourceId match), preventing echo back to our listeners.
        // isConverted=false means also run NMEA0183 conversion if configured.
        handleN2kMessage(msg, STREAM_CHANNEL_ID, false);
        
        // Update incoming counter with listener name
        if (counterIdIn >= 0) {
            api->increment(counterIdIn, listenerName);
        }
        
        if (g_logger) {
            g_logger->logDebug(GwLog::DEBUG, "StreamRx[%s]: PGN %lu from src %d", 
                              listenerName, pgn, source);
        }
        
        return true;
    }
    
    // Called from task loop
    void runCleanup() {
        for (auto& cb : cleanupCallbacks) {
            if (cb) cb();
        }
    }
    
    // Update counters for each listener (called from task loop)
    unsigned long getMessageCount() const { return messageCount; }
    
private:
    size_t countActiveListeners() const {
        size_t count = 0;
        for (const auto& entry : listeners) {
            if (entry.active) count++;
        }
        return count;
    }
    
    void dispatchToListeners(const char* msg, size_t len) {
        xSemaphoreTake(listenerLock, portMAX_DELAY);
        
        for (auto& entry : listeners) {
            if (entry.active && entry.callback) {
                entry.callback(msg, len);
                // Increment outgoing counter immediately per message per listener
                if (api && counterIdOut >= 0) {
                    api->increment(counterIdOut, entry.name.c_str());
                }
            }
        }
        
        xSemaphoreGive(listenerLock);
    }
    
    /**
     * Dispatch to all listeners EXCEPT the one specified by excludeHandle.
     * Used for incoming message distribution to prevent echo to sender.
     */
    void dispatchToListenersExcept(const char* msg, size_t len, HalmetStreamHandle excludeHandle) {
        xSemaphoreTake(listenerLock, portMAX_DELAY);
        
        for (auto& entry : listeners) {
            // Skip the sender to prevent echo
            if (entry.handle == excludeHandle) continue;
            
            if (entry.active && entry.callback) {
                entry.callback(msg, len);
                // Increment outgoing counter (incoming msg is outgoing to other listeners)
                if (api && counterIdOut >= 0) {
                    api->increment(counterIdOut, entry.name.c_str());
                }
            }
        }
        
        xSemaphoreGive(listenerLock);
    }
};

static HalmetStreamChannelImpl* g_streamImpl = nullptr;

// Global accessor
HalmetStreamInterface* getHalmetStreamInterface() {
    return g_streamImpl;
}

void halmetStreamTask(GwApi* api) {
    GwLog* logger = api->getLogger();
    logger->logDebug(GwLog::LOG, "HalmetStream: task running");
    
    // Register counters for bidirectional message tracking
    // Stream: outgoing N2K → listeners (WS, BLE sub-keys)
    // StreamRx: incoming listeners → N2K (WS, BLE sub-keys)
    int counterIdOut = api->addCounter("Stream");
    int counterIdIn = api->addCounter("StreamRx");
    
    // Set counters for immediate per-message counting
    if (g_streamImpl) {
        g_streamImpl->setCounters(api, counterIdOut, counterIdIn);
    }
    
    while (true) {
        delay(5000);
        
        // Run registered cleanup callbacks (WS cleanupClients, BLE health check, etc.)
        if (g_streamImpl) {
            g_streamImpl->runCleanup();
        }
    }
    
    vTaskDelete(NULL);
}

void halmetStreamInit(GwApi* api) {
    g_logger = api->getLogger();
    g_logger->logDebug(GwLog::LOG, "HalmetStream: init starting");
    
    // Create shared channel implementation
    g_streamImpl = new HalmetStreamChannelImpl();
    g_logger->logDebug(GwLog::LOG, "HalmetStream: impl created at %p", g_streamImpl);
    
    // Create GwChannel wrapper
    g_streamChannel = new GwChannel(g_logger, "Stream", STREAM_CHANNEL_ID, -1);
    g_streamChannel->setImpl(g_streamImpl);
    
    // Configure: start disabled, readActisense + writeActisense for bidirectional
    // Note: GwChannel creates its own ActisenseReader, but our impl's reader runs first
    // in loop() and consumes all messages, so parseActisense() finds nothing - no conflict.
    // This sets channelStream which sendActisense() needs for writing.
    g_streamChannel->begin(
        false,  // enabled - start disabled until listeners
        false,  // NMEAin
        false,  // NMEAout  
        "",     // readFilter
        "",     // writeFilter
        false,  // sendSeasmart
        false,  // toN2k
        true,   // readActisense - sets channelStream for sendActisense
        true    // writeActisense
    );
    
    channels.addChannel(g_streamChannel);
    g_logger->logDebug(GwLog::LOG, "HalmetStream: channel added");
    
    api->addUserTask(halmetStreamTask, "halmetStreamTask", 3072);
}

#endif  // BOARD_HALMET
