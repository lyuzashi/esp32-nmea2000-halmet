/**
 * WebSocket Stream Channel for Halmet
 * 
 * Standalone GwChannel implementation for WebSocket N2K streaming.
 * Uses NavLink Blue (PDGY) format for bidirectional N2K message exchange.
 * 
 * Architecture:
 * - Implements GwChannelInterface, registered directly with GwChannelList
 * - Receives N2K via sendActisense() -> buffer -> ActisenseReader -> PDGY -> WS
 * - Sends N2K via WS data -> PDGY parse -> handleN2kMessage()
 * - No dedicated task - main.cpp loop drives via channel interface
 * 
 * Endpoint: /ws
 */
#include "GwHalmetWSStreamtask.h"

#ifdef BOARD_HALMET
#ifdef WS_STREAM_ENABLED

#include "GwApi.h"
#include "GwLog.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "GwChannelInterface.h"
#include "GwWebServer.h"
#include "GwPdgyUtils.h"
#include "GwHalmetTask.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <ESPAsyncWebServer.h>

extern GwChannelList channels;
extern GwWebServer webserver;
extern void handleN2kMessage(const tN2kMsg &n2kMsg, int sourceId, bool isConverted);

#define WS_CHANNEL_ID 251
#define WS_BUFFER_SIZE 256

// Queue for deferring WS message processing (callback context safety)
#define WS_RX_QUEUE_SIZE 8
#define WS_RX_MSG_SIZE 128  // Max PDGY message length

static QueueHandle_t g_wsRxQueue = nullptr;
static GwLog* g_logger = nullptr;
static GwChannel* g_wsChannel = nullptr;

// Forward declarations
class WSChannelImpl;
static WSChannelImpl* g_wsImpl = nullptr;

/**
 * Circular buffer for ActisenseReader input.
 */
class WSCircularBuffer : public Stream {
private:
    uint8_t buffer[WS_BUFFER_SIZE];
    volatile int writePos = 0;
    volatile int readPos = 0;
    volatile int count = 0;
    
public:
    size_t write(uint8_t byte) override {
        if (count >= WS_BUFFER_SIZE) {
            // Drop oldest byte on overflow
            readPos = (readPos + 1) % WS_BUFFER_SIZE;
            count--;
        }
        buffer[writePos] = byte;
        writePos = (writePos + 1) % WS_BUFFER_SIZE;
        count++;
        return 1;
    }
    
    int available() override { return count; }
    
    int read() override { 
        if (count == 0) return -1;
        uint8_t b = buffer[readPos];
        readPos = (readPos + 1) % WS_BUFFER_SIZE;
        count--;
        return b;
    }
    
    int peek() override { 
        return (count == 0) ? -1 : buffer[readPos];
    }
};

/**
 * WebSocket channel implementation.
 * 
 * GwChannelInterface methods:
 * - getStream(): Returns buffer for sendActisense() to write Actisense binary
 * - loop(): Parses buffer -> PDGY -> broadcasts to all connected WS clients
 * - readMessages/sendToClients: Not used (we handle our own distribution)
 */
class WSChannelImpl : public GwChannelInterface {
private:
    WSCircularBuffer* stream;
    tActisenseReader reader;
    AsyncWebSocket* ws;
    unsigned long messageCount = 0;
    
public:
    WSChannelImpl() {
        stream = new WSCircularBuffer();
        reader.SetReadStream(stream);
        ws = nullptr;
    }
    
    virtual ~WSChannelImpl() {
        delete stream;
    }
    
    void setWebSocket(AsyncWebSocket* socket) {
        ws = socket;
    }
    
    AsyncWebSocket* getWebSocket() {
        return ws;
    }
    
    // GwChannelInterface implementation
    virtual void loop(bool handleRead, bool handleWrite) override {
        // Process queued incoming messages (from WS callback)
        if (g_wsRxQueue) {
            char msgBuf[WS_RX_MSG_SIZE];
            while (xQueueReceive(g_wsRxQueue, msgBuf, 0) == pdTRUE) {
                processIncoming(msgBuf);
            }
        }
        
        // Only send if we have connected clients
        if (!ws || ws->count() == 0) return;
        
        // Parse Actisense binary from buffer, convert to PDGY, send to WS clients
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            char pdgyMsg[320];
            int len = n2kMsgToPdgy(msg, pdgyMsg, sizeof(pdgyMsg));
            if (len > 0) {
                ws->textAll(pdgyMsg, len);
            }
        }
    }
    
    virtual Stream* getStream(bool) override { return stream; }
    virtual void readMessages(GwMessageFetcher*) override {}
    virtual size_t sendToClients(const char*, int, bool) override { return 0; }
    
    unsigned long getMessageCount() const { return messageCount; }
    
    /**
     * Process incoming PDGY message from a WebSocket client.
     * Parses PDGY and injects into N2K system.
     */
    bool processIncoming(const char* pdgyLine) {
        tN2kMsg msg;
        if (!pdgyToN2kMsg(pdgyLine, msg)) {
            if (g_logger) {
                g_logger->logDebug(GwLog::DEBUG, "WS: PDGY parse failed: %.30s", pdgyLine);
            }
            return false;
        }
        
        // Inject into N2K system with our channel's sourceId
        // This allows sendActisense() to skip echoing back to us
        handleN2kMessage(msg, WS_CHANNEL_ID, false);
        
        if (g_logger) {
            g_logger->logDebug(GwLog::DEBUG, "WS Rx: PGN %lu src %d", msg.PGN, msg.Source);
        }
        return true;
    }
};

// WebSocket event handler
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    
    if (type == WS_EVT_CONNECT) {
        g_logger->logDebug(GwLog::LOG, "WS: client %u connected, total=%d", 
                          client->id(), server->count());
        client->text("connected");
        
        // Enable channel when first client connects
        if (server->count() == 1 && g_wsChannel) {
            g_wsChannel->enable(true);
            g_logger->logDebug(GwLog::LOG, "WS: channel enabled");
        }
        
    } else if (type == WS_EVT_DISCONNECT) {
        g_logger->logDebug(GwLog::LOG, "WS: client %u disconnected, remaining=%d", 
                          client->id(), server->count());
        
        // Disable channel when no clients (after count is decremented)
        if (server->count() == 0 && g_wsChannel) {
            g_wsChannel->enable(false);
            g_logger->logDebug(GwLog::LOG, "WS: channel disabled");
        }
        
    } else if (type == WS_EVT_DATA) {
        // Handle incoming PDGY messages - queue for processing in main loop
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            if (len > 0 && len < WS_RX_MSG_SIZE && g_wsRxQueue) {
                char msgBuf[WS_RX_MSG_SIZE];
                memcpy(msgBuf, data, len);
                
                // Strip trailing CR/LF
                size_t plen = len;
                while (plen > 0 && (msgBuf[plen-1] == '\r' || msgBuf[plen-1] == '\n')) {
                    plen--;
                }
                msgBuf[plen] = '\0';
                
                // Queue PDGY messages for processing
                if (strncmp(msgBuf, "!PDGY,", 6) == 0) {
                    if (xQueueSend(g_wsRxQueue, msgBuf, 0) != pdTRUE) {
                        if (g_logger) {
                            g_logger->logDebug(GwLog::DEBUG, "WS: rx queue full, dropping");
                        }
                    }
                }
            }
        }
    }
}

void wsStreamInit(GwApi* api) {
    g_logger = api->getLogger();
    g_logger->logDebug(GwLog::LOG, "WS Stream: init");
    
    // Create RX queue for deferred processing (callback → main loop)
    g_wsRxQueue = xQueueCreate(WS_RX_QUEUE_SIZE, WS_RX_MSG_SIZE);
    if (!g_wsRxQueue) {
        g_logger->logDebug(GwLog::ERROR, "WS: failed to create rx queue");
        return;
    }
    
    // Create channel implementation
    g_wsImpl = new WSChannelImpl();
    
    // Create WebSocket and configure
    AsyncWebSocket* ws = new AsyncWebSocket("/ws");
    ws->onEvent(onWsEvent);
    g_wsImpl->setWebSocket(ws);
    
    // Add WebSocket handler to server
    AsyncWebServer* server = webserver.getServer();
    if (server) {
        server->addHandler(ws);
        g_logger->logDebug(GwLog::LOG, "WS Stream: /ws handler added");
    }
    
    // Create GwChannel wrapper
    g_wsChannel = new GwChannel(g_logger, "WS", WS_CHANNEL_ID, -1);
    g_wsChannel->setImpl(g_wsImpl);
    
    // Configure: start disabled, Actisense read+write for bidirectional
    g_wsChannel->begin(
        false,  // enabled - start disabled until clients connect
        false,  // NMEAin
        false,  // NMEAout
        "",     // readFilter
        "",     // writeFilter
        false,  // sendSeasmart
        false,  // toN2k (we handle this ourselves)
        true,   // readActisense - sets channelStream
        true    // writeActisense - enables sendActisense
    );
    
    // Add to channel list
    channels.addChannel(g_wsChannel);
    g_logger->logDebug(GwLog::LOG, "WS Stream: channel added (ID=%d)", WS_CHANNEL_ID);
    
    // Register cleanup micro-task with halmetTask
    halmetRegisterMicroTask("WS", []() {
        if (g_wsImpl && g_wsImpl->getWebSocket()) {
            g_wsImpl->getWebSocket()->cleanupClients();
        }
    });
}

#endif  // WS_STREAM_ENABLED
#endif  // BOARD_HALMET
