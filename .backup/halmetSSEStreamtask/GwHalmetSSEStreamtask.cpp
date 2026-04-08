/**
 * SSE Stream Task for Halmet
 * 
 * Streams NMEA2000 messages via Server-Sent Events (SSE) over HTTP.
 * Messages are formatted in NavLink Blue format for compatibility.
 * 
 * Endpoint: /api/stream/nmea
 * 
 * Based on the channel interface pattern from NavLink BLE task.
 */
#include "GwHalmetSSEStreamtask.h"

#ifdef BOARD_HALMET
#ifdef SSE_STREAM_ENABLED

#include "GwApi.h"
#include "GwLog.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "GwChannelInterface.h"
#include "GwWebServer.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <ESPAsyncWebServer.h>
#include <base64.h>

#define SSE_CHANNEL_ID 251
#define SSE_BUFFER_SIZE 128

// External references to globals in main.cpp
extern GwWebServer webserver;
extern GwChannelList channels;

// Circular buffer for ActisenseReader
class SSECircularBuffer : public Stream {
private:
    uint8_t buffer[SSE_BUFFER_SIZE];
    volatile int writePos = 0;
    volatile int readPos = 0;
    volatile int count = 0;
    
public:
    size_t write(uint8_t byte) override {
        if (count >= SSE_BUFFER_SIZE) {
            // Overflow - drop oldest byte
            readPos = (readPos + 1) % SSE_BUFFER_SIZE;
            count--;
        }
        buffer[writePos] = byte;
        writePos = (writePos + 1) % SSE_BUFFER_SIZE;
        count++;
        return 1;
    }
    
    int available() override { return count; }
    
    int read() override { 
        if (count == 0) return -1;
        uint8_t byte = buffer[readPos];
        readPos = (readPos + 1) % SSE_BUFFER_SIZE;
        count--;
        return byte;
    }
    
    int peek() override { 
        return (count == 0) ? -1 : buffer[readPos];
    }
};

// SSE Channel implementation
class SSEChannelImpl : public GwChannelInterface {
private:
    SSECircularBuffer* stream;
    tActisenseReader reader;
    AsyncEventSource* eventSource;
    unsigned long messageCount = 0;
    GwLog* logger;
    
public:
    SSEChannelImpl(AsyncEventSource* events, GwLog* log) 
        : eventSource(events), logger(log) {
        stream = new SSECircularBuffer();
        reader.SetReadStream(stream);
    }
    
    virtual ~SSEChannelImpl() {
        delete stream;
    }
    
    virtual void loop(bool handleRead, bool handleWrite) override {
        // Parse complete messages from the stream
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            // Format as NavLink Blue: !PDGY,<pgn>,<prio>,<src>,<dst>,<timer>,<base64data>
            float timer = (millis() % 100000000) / 1000.0f;
            
            // Base64 encode the data
            String encoded = base64::encode(msg.Data, msg.DataLen);
            
            // Build the message
            char sseData[200];
            snprintf(sseData, sizeof(sseData), 
                "!PDGY,%lu,%d,%d,%d,%.3f,%s",
                msg.PGN, msg.Priority, msg.Source, msg.Destination, 
                timer, encoded.c_str());
            
            // Send to all connected SSE clients
            if (eventSource && eventSource->count() > 0) {
                eventSource->send(sseData, "nmea", millis());
            }
        }
    }
    
    virtual Stream* getStream(bool /*forRead*/) override {
        return stream;
    }
    
    virtual void readMessages(GwMessageFetcher* /*writer*/) override {
        // Not used - we only send messages
    }
    
    virtual size_t sendToClients(const char* /*buffer*/, int /*sourceId*/, bool /*partial*/) override {
        // Not used
        return 0;
    }
    
    size_t getMessageCount() const { return messageCount; }
    size_t getClientCount() const { return eventSource ? eventSource->count() : 0; }
};

// Global state for SSE streaming
static AsyncEventSource* g_eventSource = nullptr;
static GwChannel* g_sseChannel = nullptr;
static SSEChannelImpl* g_sseImpl = nullptr;
static GwLog* g_logger = nullptr;

void sseStreamTask(GwApi* api) {
    GwLog* logger = api->getLogger();
    
    // Wait for main setup to complete (webserver.begin() etc)
    delay(5000);
    
    LOG_DEBUG(GwLog::LOG, "SSE Stream: task running");
    
    // Verify everything was set up during init
    if (!g_eventSource || !g_sseChannel || !g_sseImpl) {
        LOG_DEBUG(GwLog::ERROR, "SSE Stream: not properly initialized");
        vTaskDelete(NULL);
        return;
    }
    
    // NOW add the handler - after webserver.begin() has been called
    AsyncWebServer* server = webserver.getServer();
    if (server) {
        server->addHandler(g_eventSource);
        LOG_DEBUG(GwLog::LOG, "SSE Stream: handler added for /events (after begin)");
    } else {
        LOG_DEBUG(GwLog::ERROR, "SSE Stream: no server");
        vTaskDelete(NULL);
        return;
    }
    
    // Add counter for status display
    int counterId = api->addCounter("SSEStream");
    
    // Main loop - monitor client connections
    while (true) {
        delay(5000);
        
        size_t clientCount = g_eventSource ? g_eventSource->count() : 0;
        size_t msgCount = g_sseImpl ? g_sseImpl->getMessageCount() : 0;
        
        // Disable channel if no clients (save processing)
        if (g_sseChannel) {
            if (clientCount == 0 && g_sseChannel->isEnabled()) {
                g_sseChannel->enable(false);
                LOG_DEBUG(GwLog::DEBUG, "SSE channel disabled - no clients");
            }
        }
        
        // Update counter
        char status[32];
        snprintf(status, sizeof(status), "%dc/%lum", (int)clientCount, msgCount);
        api->increment(counterId, status);
        
        if (clientCount > 0) {
            LOG_DEBUG(GwLog::DEBUG, "SSE Stream: %d clients, %lu msgs", 
                      (int)clientCount, msgCount);
        }
    }
    
    vTaskDelete(NULL);
}

void sseStreamInit(GwApi* api) {
    g_logger = api->getLogger();
    GwLog* logger = g_logger;  // For LOG_DEBUG macro
    LOG_DEBUG(GwLog::LOG, "SSE Stream: initializing");
    
    // Create the event source endpoint
    g_eventSource = new AsyncEventSource("/events");
    if (!g_eventSource) {
        LOG_DEBUG(GwLog::ERROR, "SSE Stream: failed to create EventSource");
        return;
    }
    
    // Create channel implementation
    g_sseImpl = new SSEChannelImpl(g_eventSource, g_logger);
    
    // Create the channel
    g_sseChannel = new GwChannel(g_logger, "SSE", SSE_CHANNEL_ID, -1);
    g_sseChannel->setImpl(g_sseImpl);
    
    // Configure channel: writeActisense sends N2K messages to our stream
    // Start disabled until a client connects
    g_sseChannel->begin(
        false,  // enabled - start disabled
        false,  // allowRead (NMEAin)
        false,  // allowWrite (NMEAout)
        "",     // readFilter
        "",     // writeFilter
        false,  // sendSeasmart
        false,  // toN2k
        true,   // readActisense - needed for channelStream setup
        true    // writeActisense - writes N2K to our stream
    );
    
    // Register with the channel list
    channels.addChannel(g_sseChannel);
    
    // Set up connect handler for enabling channel
    g_eventSource->onConnect([](AsyncEventSourceClient* client) {
        GwLog* logger = g_logger;  // For LOG_DEBUG macro
        LOG_DEBUG(GwLog::LOG, "SSE client connected, IP: %s, clients: %d", 
                  client->client()->remoteIP().toString().c_str(),
                  g_eventSource->count());
        
        // Send a welcome message
        client->send("connected", "status", millis());
        
        // Enable channel when first client connects
        if (g_sseChannel && g_eventSource->count() == 1) {
            g_sseChannel->enable(true);
            LOG_DEBUG(GwLog::LOG, "SSE channel enabled");
        }
    });
    
    // Start the monitoring task (which will add the handler after webserver.begin())
    api->addUserTask(sseStreamTask, "sseStreamTask", 4096);
    LOG_DEBUG(GwLog::LOG, "SSE Stream: init complete");
}

#endif  // SSE_STREAM_ENABLED
#endif  // BOARD_HALMET
