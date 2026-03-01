#ifdef BOARD_HALMET
#ifdef NAVLINK_BLE_ENABLED

#include "GwApi.h"
#include "N2kMsg.h"
#include "ActisenseReader.h"
#include <base64.h>
#include "GwChannelInterface.h"
#include <NimBLECharacteristic.h>


#define NAVLINK_CHANNEL_ID 250
#define BUFFER_SIZE 128  // Optimized for typical NMEA2000 message sizes

// Base64 encoding helper - writes directly to buffer (static to avoid multiple definition)
static int base64EncodeData(const uint8_t* data, int len, char* outBuf, int outSize) {
    String encoded = base64::encode(data, len);
    int copyLen = min((int)encoded.length(), outSize - 1);
    memcpy(outBuf, encoded.c_str(), copyLen);
    outBuf[copyLen] = '\0';
    return copyLen;
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
    NimBLECharacteristic *bleCharacteristic = nullptr;
    
public:
    NavLinkChannelImpl(GwLog *logger) : logger(logger) {
        stream = new CircularBufferStream();
        reader.SetReadStream(stream);
    }

    virtual ~NavLinkChannelImpl() {
        delete stream;
    }
    
    void setBleCharacteristic(NimBLECharacteristic *characteristic) {
        bleCharacteristic = characteristic;
    }

    virtual void loop(bool handleRead, bool handleWrite) override {
        // Parse any complete messages using ActisenseReader
        tN2kMsg msg;
        while (reader.GetMessageFromStream(msg)) {
            messageCount++;
            
            // Get timer in seconds with 1ms resolution (0-99999.999)
            float timer = (millis() % 100000000) / 1000.0;
            
            // Base64 encode the data payload into buffer (max 223 bytes -> ~300 base64)
            char base64Buf[96];  // Sufficient for most messages
            base64EncodeData(msg.Data, msg.DataLen, base64Buf, sizeof(base64Buf));
            
            // Format the NavLink Blue sentence: !PDGY,<pgn#>,p,src,dst,timer,<pgn_data> CR LF
            char navLinkMsg[150];  // Optimized size
            snprintf(navLinkMsg, sizeof(navLinkMsg), 
                "!PDGY,%lu,%d,%d,%d,%.3f,%s\r\n",
                msg.PGN, msg.Priority, msg.Source, msg.Destination, 
                timer, base64Buf
            );
            
            // Send to BLE if connected
            if (bleCharacteristic != nullptr) {
                int len = strlen(navLinkMsg);
                bleCharacteristic->setValue((uint8_t*)navLinkMsg, len);
                bleCharacteristic->notify();
            }
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

#endif //NAVLINK_BLE_ENABLED
#endif // BOARD_HALMET