#include "GwLoggerTask.h"
#include "GwApi.h"
#include "GwChannelInterface.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <base64.h>

#define LOGGER_CHANNEL_ID 250
#define BUFFER_SIZE 512

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

class LoggerChannelImpl : public GwChannelInterface {
private:
    CircularBufferStream *stream;
    tActisenseReader reader;
    GwLog *logger;
    unsigned long messageCount = 0;
    
public:
    LoggerChannelImpl(GwLog *logger) : logger(logger) {
        stream = new CircularBufferStream();
        reader.SetReadStream(stream);
        logger->logDebug(GwLog::LOG, "[N2kLogger] LoggerChannelImpl constructed");
    }

    virtual ~LoggerChannelImpl() {
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
                msg.PGN,           // PGN number
                msg.Priority,      // Priority (0-7)
                msg.Source,        // Source address
                msg.Destination,   // Destination address
                timer,             // Timer in seconds
                base64Data.c_str() // Base64 encoded data
            );
            
            // Log the NavLink Blue formatted message
            logger->logDebug(GwLog::ERROR, "[NavLink] %s", navLinkMsg);
        }
    }
    
    virtual void readMessages(GwMessageFetcher *fetcher) override {
        // Not reading messages, just logging what's written
    }
    
    virtual size_t sendToClients(const char *buffer, int sourceId, bool partial) override {
        return 0;
    }
    
    virtual Stream* getStream(bool partialWrites) override {
        return stream;
    }
    
    virtual String getMode() override {
        return "N2K_LOGGER";
    }
};

void loggerInit(GwApi *api) {
    GwLog *logger = api->getLogger();
    logger->logDebug(GwLog::ERROR, "[N2kLogger] Init starting");

    LoggerChannelImpl *impl = new LoggerChannelImpl(logger);
    
    GwChannel *channel = new GwChannel(logger, "Logger", LOGGER_CHANNEL_ID, -1);
    channel->setImpl(impl);
    
    logger->logDebug(GwLog::ERROR, "[N2kLogger] Before begin(), impl=%p", impl);
    
    // NOTE: readActisense MUST be true for channelStream to be set!
    // The GwChannel::begin() code only calls impl->getStream() when readActisense=true
    // Even though we only want to write (writeActisense), we need readActisense=true too
    channel->begin(true, false, false, "", "", false, false, true, true);
    
    // Verify the stream is set
    Stream *testStream = impl->getStream(false);
    logger->logDebug(GwLog::ERROR, "[N2kLogger] After begin(), stream=%p", testStream);
    logger->logDebug(GwLog::ERROR, "[N2kLogger] Channel: %s", channel->toString().c_str());

    extern GwChannelList channels;
    channels.addChannel(channel);

    logger->logDebug(GwLog::ERROR, "[N2kLogger] Channel ready - will parse N2K messages in loop");
    
    api->addCapability("n2kLogger", "true");
}

DECLARE_INITFUNCTION(loggerInit);