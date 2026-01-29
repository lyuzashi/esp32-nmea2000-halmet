#ifdef ENABLE_NAVLINKTASK

#include "GwNavlinkTask.h"
#include "GwApi.h"
#include "GwChannelInterface.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <base64.h>


#define NAVLINK_CHANNEL_ID 250
#define BUFFER_SIZE 512
#define MESSAGE_QUEUE_SIZE 50


// Base64 encoding helper
String base64EncodeData(const uint8_t* data, int len) {
    return base64::encode(data, len);
}

/**
 * Circular buffer that ActisenseReader can read from
 */
class CircularBufferStream : public Stream {
private:
    uint8_t buffer[BUFFER_SIZE];
    int writePos = 0;
    int readPos = 0;
    int count = 0;
    
public:
    virtual size_t write(uint8_t byte) override {
        if (count >= BUFFER_SIZE) {
            readPos = (readPos + 1) % BUFFER_SIZE;
            count--;
        }
        buffer[writePos] = byte;
        writePos = (writePos + 1) % BUFFER_SIZE;
        count++;
        return 1;
    }
    
    virtual int available() override { return count; }
    
    virtual int read() override { 
        if (count == 0) return -1;
        uint8_t byte = buffer[readPos];
        readPos = (readPos + 1) % BUFFER_SIZE;
        count--;
        return byte;
    }
    
    virtual int peek() override { 
        if (count == 0) return -1;
        return buffer[readPos];
    }
};

class NavLinkChannelImpl : public GwChannelInterface {
private:
    CircularBufferStream *stream;
    tActisenseReader reader;
    GwLog *logger;
    unsigned long messageCount = 0;
    
    // Message queue for BLE notifications
    struct QueuedMessage {
        char data[256];
        bool ready;
    };
    QueuedMessage messageQueue[MESSAGE_QUEUE_SIZE];
    int queueWrite = 0;
    int queueRead = 0;
    
public:
    NavLinkChannelImpl(GwLog *logger) : logger(logger) {
        stream = new CircularBufferStream();
        reader.SetReadStream(stream);
        logger->logDebug(GwLog::LOG, "[NavLink] NavLinkChannelImpl constructed");
        
        // Initialize queue
        for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
            messageQueue[i].ready = false;
        }
    }

    virtual ~NavLinkChannelImpl() {
        delete stream;
    }

    virtual void loop(bool handleRead, bool handleWrite) override {
        // Parse any complete messages using ActisenseReader
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            // Generate NavLink Blue RX format:
            // !PDGY,<pgn#>,p,src,dst,timer,<pgn_data> CR LF
            
            // Get timer in seconds with 1ms resolution (0-99999.999)
            float timer = (millis() % 100000000) / 1000.0;
            
            // Base64 encode the data payload
            String base64Data = base64EncodeData(msg.Data, msg.DataLen);
            
            // Format the NavLink Blue sentence
            char navLinkMsg[256];
            snprintf(navLinkMsg, sizeof(navLinkMsg), 
                "!PDGY,%lu,%d,%d,%d,%.3f,%s\r\n",
                msg.PGN, msg.Priority, msg.Source, msg.Destination, 
                timer, base64Data.c_str()
            );
            
            // Queue message for BLE notification
            int idx = queueWrite % MESSAGE_QUEUE_SIZE;
            if (!messageQueue[idx].ready) {
                strncpy(messageQueue[idx].data, navLinkMsg, sizeof(messageQueue[idx].data) - 1);
                messageQueue[idx].ready = true;
                queueWrite++;
            }
            
            // For now, just log to serial since BLE conflicts with WiFi
            Serial.print(navLinkMsg);
        }
        
        // Clear old messages from queue
        while (queueRead < queueWrite) {
            int idx = queueRead % MESSAGE_QUEUE_SIZE;
            if (!messageQueue[idx].ready) break;
            messageQueue[idx].ready = false;
            queueRead++;
        }
    }

    virtual Stream* getStream(bool /*forRead*/) override {
        return stream;
    }

    virtual void readMessages(GwMessageFetcher *writer) override {
        // Not used - we only write messages
    }

    virtual size_t sendToClients(const char *buffer, int sourceId, bool partial=false) override {
        // Not used - we receive via stream and send via BLE
        return 0;
    }

    size_t write(uint8_t c) {
        return stream->write(c);
    }

    size_t write(const uint8_t *buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            stream->write(buffer[i]);
        }
        return size;
    }
};

void navlinkInit(GwApi *api) {
    GwLog *logger = api->getLogger();
    logger->logDebug(GwLog::ERROR, "[NavLink] Init starting");

    // Create channel first (without BLE for now)
    NavLinkChannelImpl *impl = new NavLinkChannelImpl(logger);
    
    GwChannel *channel = new GwChannel(logger, "NavLink", NAVLINK_CHANNEL_ID, -1);
    channel->setImpl(impl);
    
    // NOTE: readActisense MUST be true for channelStream to be set!
    channel->begin(true, false, false, "", "", false, false, true, true);
    
    extern GwChannelList channels;
    channels.addChannel(channel);

    logger->logDebug(GwLog::ERROR, "[NavLink] Channel ready - streaming NavLink format to serial");
    
    api->addCapability("navLinkBlue", "serial-only");


}

DECLARE_INITFUNCTION(navlinkInit);

#endif // ENABLE_NAVLINKTASK