#ifdef ENABLE_TCPLOGTASK

#include "GwTcpLogTask.h"
#include "GwLog.h"
#include <WiFi.h>

#define TCP_LOG_PORT 8880

/**
 * TCP Log Writer that streams logs over network
 * Compatible with Microsoft Serial Monitor: tcp://ip:port
 */
class TcpLogWriter : public GwLogWriter {
private:
    WiFiServer *server = nullptr;
    WiFiClient client;
    uint16_t port;
    bool serverStarted = false;
    GwLog *logger = nullptr;
    unsigned long lastConnectionCheck = 0;
    static const unsigned long CONNECTION_CHECK_INTERVAL = 1000; // Check every second
    
public:
    TcpLogWriter(GwLog *logger, uint16_t port) : port(port), logger(logger) {}
    
    virtual ~TcpLogWriter() {
        stop();
    }
    
    void begin() {
        if (serverStarted) {
            if (logger) {
                logger->logDebug(GwLog::DEBUG, "[TcpLog] Server already started");
            }
            return;
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            if (logger) {
                logger->logDebug(GwLog::DEBUG, "[TcpLog] Cannot start server - WiFi not connected (status=%d)", WiFi.status());
            }
            return;
        }
        
        if (logger) {
            logger->logDebug(GwLog::LOG, "[TcpLog] Starting server on port %d...", port);
        }
        
        server = new WiFiServer(port);
        server->begin();
        server->setNoDelay(true);  // Disable Nagle's algorithm for real-time logging
        serverStarted = true;
        
        if (logger) {
            logger->logDebug(GwLog::LOG, 
                "[TcpLog] Server started on %s:%d", 
                WiFi.localIP().toString().c_str(), 
                port);
            logger->logDebug(GwLog::LOG, 
                "[TcpLog] Connect with: tcp://%s:%d",
                WiFi.localIP().toString().c_str(), 
                port);
        }
    }
    
    void stop() {
        if (client) {
            client.stop();
        }
        if (server) {
            server->stop();
            delete server;
            server = nullptr;
        }
        serverStarted = false;
    }
    
    void loop() {
        unsigned long now = millis();
        
        // Only check periodically to avoid overhead
        if (now - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) {
            return;
        }
        lastConnectionCheck = now;
        
        if (!serverStarted || !server) {
            // Try to start if WiFi is now connected
            if (WiFi.status() == WL_CONNECTED) {
                begin();
            }
            return;
        }
        
        // Accept new client if current one is disconnected
        if (!client || !client.connected()) {
            WiFiClient newClient = server->available();
            if (newClient) {
                if (client) {
                    client.stop();
                }
                client = newClient;
                if (logger) {
                    logger->logDebug(GwLog::LOG, 
                        "[TcpLog] Client connected from %s", 
                        client.remoteIP().toString().c_str());
                }
            }
        }
    }
    
    virtual void write(const char *data) override {
        if (client && client.connected()) {
            client.print(data);
        }
    }
    
    virtual void flush() override {
        if (client && client.connected()) {
            client.flush();
        }
    }
    
    bool isClientConnected() {
        return client && client.connected();
    }
    
    uint16_t getPort() {
        return port;
    }
};

// Global instance
static TcpLogWriter *tcpLogWriter = nullptr;

/**
 * Task function - runs continuously to handle TCP connections
 * This follows the pattern from exampletask
 */
void tcpLogTask(GwApi *api) {
    GwLog *logger = api->getLogger();
    
    bool serverStarted = false;
    bool writerInstalled = false;
    
    LOG_DEBUG(GwLog::LOG, "[TcpLog] Task started, waiting for WiFi...");
    
    // Task loop - similar to exampletask pattern
    while (true) {
        delay(1000);
        
        if (tcpLogWriter) {
            // Try to start server if not started yet
            if (!serverStarted && WiFi.status() == WL_CONNECTED) {
                LOG_DEBUG(GwLog::LOG, "[TcpLog] WiFi connected, starting TCP server on port %d", TCP_LOG_PORT);
                tcpLogWriter->begin();
                serverStarted = true;
                
                // Now that server is started, install the writer
                // This ensures logs will go to TCP from this point forward
                if (!writerInstalled) {
                    logger->setWriter(tcpLogWriter);
                    writerInstalled = true;
                    LOG_DEBUG(GwLog::LOG, "[TcpLog] Log writer installed - logs will stream to TCP clients");
                }
            }
            
            // Handle client connections
            tcpLogWriter->loop();
        }
    }
    
    // Clean exit (though we never reach here in normal operation)
    vTaskDelete(NULL);
}

/**
 * Init function called during system startup
 */
void tcpLogInit(GwApi *api) {
    GwLog *logger = api->getLogger();
    
    // Create TCP log writer but DON'T set it as the writer yet
    // to avoid circular reference during initialization
    tcpLogWriter = new TcpLogWriter(logger, TCP_LOG_PORT);
    
    // Register the task function (not a class) - following exampletask pattern
    const String taskName("tcpLogTask");
    api->addUserTask(tcpLogTask, taskName, 2000);
    
    // Add capability for detection
    api->addCapability("tcpLog", String(TCP_LOG_PORT));
}

#endif // ENABLE_TCPLOGTASK
