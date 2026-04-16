/**
 * GwChannel.cpp - Halmet optimized version
 * 
 * Changes from original:
 * - Lazy receiver allocation: GwChannelMessageReceiver only allocated when nmeaIn=true
 *   Saves ~604 bytes per channel that only uses Actisense (no NMEA0183 parsing)
 * - Optional counters: Define HALMET_CHANNEL_NO_COUNTERS to disable per-PGN counters
 *   Saves ~160+ bytes base + ~68 bytes per unique PGN
 * - Stubbed toJson/toString: Define HALMET_CHANNEL_NO_JSON to minimize these
 * - No NMEA0183: Define HALMET_CHANNEL_NO_NMEA0183 to remove NMEA0183 parsing entirely
 *   Saves ~700 bytes code + removes GwChannelMessageReceiver class
 * 
 * All other functionality identical to upstream.
 */

// Halmet optimization flags (can be set in platformio.ini build_flags)
// #define HALMET_CHANNEL_NO_COUNTERS    // Disable message counters
// #define HALMET_CHANNEL_NO_JSON        // Stub out JSON/string serialization
// #define HALMET_CHANNEL_NO_NMEA0183    // Remove NMEA0183 parsing entirely

#include "GwChannel.h"
#include <ActisenseReader.h>

// HALMET: Forward declaration for handleN2kMessage (used by bidirectional channels)
extern void handleN2kMessage(const tN2kMsg &n2kMsg, int sourceId, bool isConverted);

#ifndef HALMET_CHANNEL_NO_NMEA0183
// Only compile NMEA0183 receiver if needed
class GwChannelMessageReceiver : public GwMessageFetcher{
  static const int bufferSize=GwBuffer::RX_BUFFER_SIZE+4;
  uint8_t buffer[bufferSize];
  uint8_t *writePointer=buffer;
  GwLog *logger;
  GwChannel *channel;
  GwChannel::NMEA0183Handler handler;
  public:
    GwChannelMessageReceiver(GwLog *logger,GwChannel *channel){
        this->logger=logger;
        this->channel=channel;
    }
    void setHandler(GwChannel::NMEA0183Handler handler){
        this->handler=handler;
    }
    virtual bool handleBuffer(GwBuffer *gwbuffer){
      size_t len=fetchMessageToBuffer(gwbuffer,buffer,bufferSize-4,'\n');
      writePointer=buffer+len;
      if (writePointer == buffer) return false;
      uint8_t *p;
      for (p=writePointer-1;p>=buffer && *p <= 0x20;p--){
        *p=0;
      }
      if (p > buffer){
        p++;
        *p=0x0d;
        p++;
        *p=0x0a;
        p++;
        *p=0;
      }
      for (p=buffer; *p != 0 && p < writePointer && *p <= 0x20;p++){}
      //very simple NMEA check
      if (*p != '!' && *p != '$'){
        LOG_DEBUG(GwLog::DEBUG,"unknown line [%d] - ignore: %s",id,(const char *)p);  
      }
      else{
        LOG_DEBUG(GwLog::DEBUG,"NMEA[%d]: %s",id,(const char *)p);
        if (channel->canReceive((const char *)p)){
            handler((const char *)p,id);
        }
      }
      writePointer=buffer;
      return true;
    }
};
#endif // HALMET_CHANNEL_NO_NMEA0183


GwChannel::GwChannel(GwLog *logger,
    String name,
    int sourceId,
    int maxSourceId){
    this->logger = logger;
    this->name=name;
    this->sourceId=sourceId;
    this->maxSourceId=maxSourceId;
    this->impl=NULL;
    // HALMET: Lazy allocation - don't allocate receiver here
    // Will be allocated in begin() only if nmeaIn is true
    this->receiver=NULL;
    this->actisenseReader=NULL;
}
void GwChannel::begin(
    bool enabled,
    bool nmeaOut,
    bool nmeaIn,
    String readFilter,
    String writeFilter,
    bool seaSmartOut,
    bool toN2k,
    bool readActisense,
    bool writeActisense)
{
    this->enabled = enabled;
    this->NMEAout = nmeaOut;
    this->NMEAin = nmeaIn;
    this->readFilter=readFilter.isEmpty()?
        NULL:
        new GwNmeaFilter(readFilter);
    this->writeFilter=writeFilter.isEmpty()?
        NULL:
        new GwNmeaFilter(writeFilter);
    this->seaSmartOut=seaSmartOut;
    this->toN2k=toN2k;
    this->readActisense=readActisense;
    this->writeActisense=writeActisense;
    
    // HALMET: Allocate receiver only when actually needed for NMEA0183 input
#ifndef HALMET_CHANNEL_NO_NMEA0183
    if (nmeaIn && !this->receiver) {
        this->receiver = new GwChannelMessageReceiver(logger, this);
    }
#endif
    
    if (impl && readActisense){
        channelStream=impl->getStream(false);
        if (! channelStream) {
            this->readActisense=false;
            this->writeActisense=false;
            LOG_DEBUG(GwLog::ERROR,"unable to read actisnse on %s",name.c_str());
        }
        else{
            this->actisenseReader= new tActisenseReader();
            actisenseReader->SetReadStream(channelStream);         
        }
    }
    
#ifndef HALMET_CHANNEL_NO_COUNTERS
    if (nmeaIn || readActisense){
        this->countIn=new GwCounter<String>(String("count")+name+String("in"));
    }
    if (nmeaOut || seaSmartOut || writeActisense){
        this->countOut=new GwCounter<String>(String("count")+name+String("out"));
    }
#endif
}
void GwChannel::setImpl(GwChannelInterface *impl){
    this->impl=impl;
}
void GwChannel::updateCounter(const char *msg, bool out)
{
#if defined(HALMET_CHANNEL_NO_COUNTERS) || defined(HALMET_CHANNEL_NO_NMEA0183)
    return;  // HALMET: Counters or NMEA0183 disabled
#else
    char key[7];
    key[0]=0;
    if (msg[0] == '$')
    {
        for (int i=0;i<6 && msg[i] != 0;i++){
            if (i>=3) {
                if (isalnum(msg[i]))key[i-3]=msg[i];
                else key[i-3]='_';
            }
            key[i-2]=0;
        }
        key[3] = 0;
    }
    else if (msg[0] == '!')
    {
        for (int i=0;i<6 && msg[i] != 0;i++){
            if (i>=1) {
                if (isalnum(msg[i]))key[i-1]=msg[i];
                else key[i-1]='_';
            }
            key[i]=0;
        }
        key[5] = 0;
    }
    else{
        return;
    }
    if (key[0] == 0) return;
    if (out){
        if (countOut) countOut->add(key);
    }
    else{
        if (countIn) countIn->add(key);
    }
#endif
}

bool GwChannel::canSendOut(const char *buffer, bool isSeasmart){
#ifdef HALMET_CHANNEL_NO_NMEA0183
    return false;  // HALMET: No NMEA0183 output
#else
    if (! enabled || ! impl) return false;
    if (readActisense) return false;
    if (! isSeasmart && ! NMEAout) return false;
    if (isSeasmart && ! seaSmartOut) return false;
    if (writeFilter && ! writeFilter->canPass(buffer)) return false;
    return true;
#endif
}

bool GwChannel::canReceive(const char *buffer){
#ifdef HALMET_CHANNEL_NO_NMEA0183
    return false;  // HALMET: No NMEA0183 input
#else
    if (! enabled) return false;
    if (! NMEAin) return false;
    if (readFilter && ! readFilter->canPass(buffer)) return false;
    updateCounter(buffer,false);
    return true;
#endif
}

int GwChannel::getJsonSize(){
#ifdef HALMET_CHANNEL_NO_JSON
    return JSON_OBJECT_SIZE(3);  // HALMET: Minimal size
#else
    int rt=JSON_OBJECT_SIZE(6);
    if (countIn) rt+=countIn->getJsonSize();
    if (countOut) rt+=countOut->getJsonSize();
    return rt;
#endif
}

void GwChannel::toJson(GwJsonDocument &doc){
#ifdef HALMET_CHANNEL_NO_JSON
    // HALMET: Minimal JSON - just id
    JsonObject jo=doc.createNestedObject("ch"+name);
    jo["id"]=sourceId;
#else
    JsonObject jo=doc.createNestedObject("ch"+name);
    jo["id"]=sourceId;
    jo["max"]=maxSourceId;
    if (countOut) countOut->toJson(doc);
    if (countIn) countIn->toJson(doc);
#endif
}

String GwChannel::toString(){
#ifdef HALMET_CHANNEL_NO_JSON
    // HALMET: Minimal string
    return String("CH") + name + "(" + sourceId + ")";
#else
    String rt="CH"+name+"("+sourceId+"):";
    rt+=enabled?"[ena]":"[dis]";
    rt+=NMEAin?"in,":"";
    rt+=NMEAout?"out,":"";
    rt+=String("RF:") + (readFilter?readFilter->toString():"[]");
    rt+=String("WF:") + (writeFilter?writeFilter->toString():"[]");
    rt+=String(",")+ (toN2k?"n2k":"");
    rt+=String(",")+ (seaSmartOut?"SM":"");
    rt+=String(",")+(readActisense?"AR":"");
    rt+=String(",")+(writeActisense?"AW":"");
    return rt;
#endif
}

void GwChannel::loop(bool handleRead, bool handleWrite){
    // HALMET: Process TX queue first (runs even without impl for callback-only channels)
    if (enabled && txProcessor) {
        txProcessor();
    }
    // HALMET: Process RX queue (runs even without impl for callback-only channels)
    if (enabled && rxProcessor) {
        rxProcessor();
    }
    
    // HALMET: Process bidirectional queues if configured
    if (enabled && isBidirectional()) {
        // Process TX queue
        if (isConnected && isConnected()) {
            QueuedTxMsg txMsg;
            int sent = 0;
            while (sent < 10 && xQueueReceive(txQueue, &txMsg, 0) == pdTRUE) {
                // Reconstruct tN2kMsg
                tN2kMsg msg;
                msg.Init(txMsg.priority, txMsg.pgn, txMsg.source, txMsg.dest);
                for (int i = 0; i < txMsg.dataLen; i++) msg.AddByte(txMsg.data[i]);
                
                // Format and send
                char buf[320];
                int len = formatter(msg, buf, sizeof(buf));
                if (len > 0 && sender) {
                    sender(buf, len);
                    sent++;
                }
            }
        }
        
        // Process RX queue
        if (rxQueue && rxMsgSize > 0) {
            char* msgBuf = (char*)alloca(rxMsgSize);
            int processed = 0;
            while (processed < 10 && xQueueReceive(rxQueue, msgBuf, 0) == pdTRUE) {
                tN2kMsg msg;
                if (parser && parser(msgBuf, msg)) {
                    handleN2kMessage(msg, sourceId, false);
                }
                processed++;
            }
        }
    }
    
    if (! enabled || ! impl) return;
    impl->loop(handleRead,handleWrite);
}
void GwChannel::readMessages(GwChannel::NMEA0183Handler handler){
#ifdef HALMET_CHANNEL_NO_NMEA0183
    return;  // HALMET: No NMEA0183 input
#else
    if (! enabled || ! impl) return;
    if (readActisense || ! NMEAin) return;
    // HALMET: Check receiver exists (lazy allocation)
    if (! receiver) return;
    receiver->id=sourceId; 
    receiver->setHandler(handler);
    impl->readMessages(receiver);
#endif
}
void GwChannel::sendToClients(const char *buffer, int sourceId, bool isSeasmart){
#ifdef HALMET_CHANNEL_NO_NMEA0183
    return;  // HALMET: No NMEA0183 output
#else
    if (! impl) return;
    if (canSendOut(buffer,isSeasmart)){
        if(impl->sendToClients(buffer,sourceId)){
            updateCounter(buffer,true);
        }
    }
#endif
}
void GwChannel::parseActisense(N2kHandler handler){
    if (!enabled || ! impl || ! readActisense || ! actisenseReader) return;
    tN2kMsg N2kMsg;

    while (actisenseReader->GetMessageFromStream(N2kMsg)) {
#ifndef HALMET_CHANNEL_NO_COUNTERS
      if(countIn) countIn->add(String(N2kMsg.PGN));
#endif
      handler(N2kMsg,sourceId);
    }
}

void GwChannel::sendActisense(const tN2kMsg &msg, int sourceId){
    if (!enabled) return;
    
    // Source ID filtering
    if (maxSourceId < 0 && this->sourceId == sourceId) return;
    if (sourceId >= this->sourceId && sourceId <= maxSourceId) return;
    
#ifndef HALMET_CHANNEL_NO_COUNTERS
    if(countOut) countOut->add(String(msg.PGN)); 
#endif

    // HALMET: Direct callback (for callback-only channels)
    if (n2kCallback) {
        n2kCallback(msg, sourceId);
    }
    
    // Actisense stream output (normal path)
    if (impl && writeActisense && channelStream) {
        msg.SendInActisenseFormat(channelStream);
    }
}

bool GwChannel::overlaps(const GwChannel *other) const{
    if (maxSourceId < 0){
        if (other->maxSourceId < 0) return sourceId == other->sourceId;
        return (other->sourceId <= sourceId && other->maxSourceId >= sourceId);
    }
    if (other->maxSourceId < 0){
        return other->sourceId >= sourceId && other->sourceId <= maxSourceId;
    }
    if (other->maxSourceId < sourceId) return false;
    if (other->sourceId > maxSourceId) return false;
    return true;
}

unsigned long GwChannel::countRx(){
#ifdef HALMET_CHANNEL_NO_COUNTERS
    return 0UL;
#else
    if (! countIn) return 0UL;
    return countIn->getGlobal();
#endif
}
unsigned long GwChannel::countTx(){
#ifdef HALMET_CHANNEL_NO_COUNTERS
    return 0UL;
#else
    if (! countOut) return 0UL;
    return countOut->getGlobal();
#endif
}
String GwChannel::typeString(int type){
    switch (type){
        case GWSERIAL_TYPE_UNI:
            return "UNI";
        case GWSERIAL_TYPE_BI:
            return "BI";
        case GWSERIAL_TYPE_RX:
            return "RX";
        case GWSERIAL_TYPE_TX:
            return "TX";
    }
    return "UNKNOWN";
}

// HALMET: Bidirectional channel implementation
bool GwChannel::beginBidirectional(
    N2kFormatter formatter,
    N2kParser parser,
    TransportSender sender,
    ConnectionCheck isConnected,
    int txQueueSize,
    int rxQueueSize,
    size_t rxMsgSize
) {
    // Create TX queue
    txQueue = xQueueCreate(txQueueSize, sizeof(QueuedTxMsg));
    if (!txQueue) {
        if (logger) logger->logDebug(GwLog::ERROR, "%s: failed to create tx queue", name.c_str());
        return false;
    }
    
    // Create RX queue
    this->rxMsgSize = rxMsgSize;
    rxQueue = xQueueCreate(rxQueueSize, rxMsgSize);
    if (!rxQueue) {
        if (logger) logger->logDebug(GwLog::ERROR, "%s: failed to create rx queue", name.c_str());
        vQueueDelete(txQueue);
        txQueue = nullptr;
        return false;
    }
    
    // Store callbacks
    this->formatter = formatter;
    this->parser = parser;
    this->sender = sender;
    this->isConnected = isConnected;
    
    // Set up the N2K callback to queue outgoing messages
    this->n2kCallback = [this](const tN2kMsg& msg, int srcId) {
        if (!this->txQueue) return;
        if (this->isConnected && !this->isConnected()) return;
        
        QueuedTxMsg txMsg;
        txMsg.pgn = msg.PGN;
        txMsg.priority = msg.Priority;
        txMsg.source = msg.Source;
        txMsg.dest = msg.Destination;
        txMsg.dataLen = (msg.DataLen > 8) ? 8 : msg.DataLen;
        memcpy(txMsg.data, msg.Data, txMsg.dataLen);
        
        xQueueSend(this->txQueue, &txMsg, 0);
    };
    
    this->enabled = true;
    return true;
}

bool GwChannel::queueIncoming(const char* data, size_t len) {
    if (!rxQueue || !data || len == 0 || len >= rxMsgSize) return false;
    
    // Create null-terminated copy
    char* msgBuf = (char*)alloca(rxMsgSize);
    memcpy(msgBuf, data, len);
    
    // Strip trailing CRLF
    while (len > 0 && (msgBuf[len-1] == '\r' || msgBuf[len-1] == '\n')) {
        len--;
    }
    msgBuf[len] = '\0';
    
    return xQueueSend(rxQueue, msgBuf, 0) == pdTRUE;
}
