#pragma once
#include "GwChannelInterface.h"
#include "GwConfigItem.h"
#include "GwLog.h"
#include "GWConfig.h"
#include "GwCounter.h"
#include "GwJsonDocument.h"
#include <N2kMsg.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class GwChannelMessageReceiver;
class tActisenseReader;
class GwChannel{
    bool enabled=false;
    bool NMEAout=false;
    bool NMEAin=false;
    GwNmeaFilter* readFilter=NULL;
    GwNmeaFilter* writeFilter=NULL;
    bool seaSmartOut=false;
    bool toN2k=false;
    bool readActisense=false;
    bool writeActisense=false;
    GwLog *logger;
    String name;
    GwCounter<String> *countIn=NULL;
    GwCounter<String> *countOut=NULL;
    GwChannelInterface *impl;
    int sourceId=0;
    int maxSourceId=-1;
    GwChannelMessageReceiver *receiver=NULL;
    tActisenseReader *actisenseReader=NULL;
    Stream *channelStream=NULL;
    void updateCounter(const char *msg, bool out);
    public:
    // N2K callback type
    typedef std::function<void(const tN2kMsg &msg, int sourceId)> N2kHandler;
    // TX processor callback - called from main loop for fast TX queue draining
    typedef std::function<void()> TxProcessor;
    // RX processor callback - called from main loop for safe RX queue draining
    typedef std::function<void()> RxProcessor;
    
    // Bidirectional channel types (HALMET)
    // Formatter: N2K message → formatted string for transport, returns length or -1
    typedef std::function<int(const tN2kMsg& msg, char* buf, size_t bufLen)> N2kFormatter;
    // Parser: formatted string → N2K message, returns true on success
    typedef std::function<bool(const char* line, tN2kMsg& msg)> N2kParser;
    // Sender: send formatted data to transport clients, returns true on success
    typedef std::function<bool(const char* data, size_t len)> TransportSender;
    // Connection check: returns true if transport has connected clients
    typedef std::function<bool()> ConnectionCheck;
    
    // Compact queued TX message (fits most N2K messages)  
    struct QueuedTxMsg {
        unsigned long pgn;
        uint8_t priority;
        uint8_t source;
        uint8_t dest;
        uint8_t dataLen;
        uint8_t data[8];
    };
    
    private:
    N2kHandler n2kCallback = nullptr;
    TxProcessor txProcessor = nullptr;
    RxProcessor rxProcessor = nullptr;
    
    // Bidirectional queue members (HALMET)
    QueueHandle_t txQueue = nullptr;
    QueueHandle_t rxQueue = nullptr;
    N2kFormatter formatter = nullptr;
    N2kParser parser = nullptr;
    TransportSender sender = nullptr;
    ConnectionCheck isConnected = nullptr;
    size_t rxMsgSize = 0;
    
    public:
    GwChannel(
        GwLog *logger,
        String name,
        int sourceId,
        int maxSourceId=-1);
    void begin(
        bool enabled,
        bool nmeaOut,
        bool nmeaIn,
        String readFilter,
        String writeFilter,
        bool seaSmartOut,
        bool toN2k,
        bool readActisense=false,
        bool writeActisense=false
    );
    
    // HALMET: Lightweight callback-only channel (no impl, no buffers, no counters)
    void beginCallbackOnly(N2kHandler callback) {
        this->enabled = true;
        this->n2kCallback = callback;
    }
    
    // HALMET: Set TX processor for fast queue draining from main loop
    void setTxProcessor(TxProcessor processor) {
        this->txProcessor = processor;
    }
    
    // HALMET: Set RX processor for safe queue draining from main loop
    void setRxProcessor(RxProcessor processor) {
        this->rxProcessor = processor;
    }
    
    /**
     * HALMET: Initialize bidirectional queued channel.
     * Channel manages TX/RX queues internally. Transport just provides:
     * - formatter: convert tN2kMsg to string for sending
     * - parser: convert received string to tN2kMsg
     * - sender: send formatted string to transport clients
     * - isConnected: check if transport has clients
     * 
     * Transport calls queueIncoming() when data arrives.
     * Channel processes queues in loop() and calls handleN2kMessage().
     */
    bool beginBidirectional(
        N2kFormatter formatter,
        N2kParser parser, 
        TransportSender sender,
        ConnectionCheck isConnected,
        int txQueueSize = 16,
        int rxQueueSize = 4,
        size_t rxMsgSize = 256
    );
    
    /**
     * HALMET: Queue incoming data from transport for processing.
     * Called from transport's receive callback (any context - will be queued).
     * Returns true if queued successfully.
     */
    bool queueIncoming(const char* data, size_t len);
    
    /**
     * HALMET: Check if channel has bidirectional queues configured.
     */
    bool isBidirectional() const { return txQueue != nullptr && rxQueue != nullptr; }

    void setImpl(GwChannelInterface *impl);
    bool overlaps(const GwChannel *) const;
    void enable(bool enabled){
        this->enabled=enabled;
    }
    bool isEnabled(){return enabled;}
    bool shouldRead(){return enabled && NMEAin;}
    bool canSendOut(const char *buffer, bool isSeasmart);
    bool canReceive(const char *buffer);
    bool sendSeaSmart(){ return seaSmartOut;}
    bool sendToN2K(){return toN2k;}
    int getJsonSize();
    void toJson(GwJsonDocument &doc);
    String toString();

    void loop(bool handleRead, bool handleWrite);
    typedef std::function<void(const char *buffer, int sourceid)> NMEA0183Handler;
    void readMessages(NMEA0183Handler handler);
    void sendToClients(const char *buffer, int sourceId, bool isSeasmart=false);
    // N2kHandler typedef moved to private section above
    void parseActisense(N2kHandler handler);
    void sendActisense(const tN2kMsg &msg, int sourceId);
    // HALMET: Set callback for direct N2K messages (bypasses Actisense stream)
    void setN2kCallback(N2kHandler handler) { n2kCallback = handler; }
    unsigned long countRx();
    unsigned long countTx();
    bool isOwnSource(int source){
        if (maxSourceId < 0) return source == sourceId;
        return (source >= sourceId && source <= maxSourceId);
    }
    static String typeString(int type);
    String getMode(){return typeString(impl->getType());}
    int getMinId(){return sourceId;};
};

