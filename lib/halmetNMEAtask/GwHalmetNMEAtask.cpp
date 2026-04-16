/**
 * Halmet NMEA Task - Proof of Concept
 * 
 * Direct hook into NMEA2000 library using tMsgHandler.
 * Logs all N2K messages received to verify the hook is working.
 */

#include "GwHalmetNMEAtask.h"

#ifdef BOARD_HALMET

#include "GwHalmetTask.h"  // For halmetRegisterMicroTask
#include "NMEA2000.h"
#include "Nmea2kTwai.h"
#include "GwLog.h"

// External NMEA2000 object from main.cpp
extern Nmea2kTwai &NMEA2000;

// Logger
static GwLog* LOG = nullptr;

// Rate limiting for logging
static unsigned long lastLogTime = 0;
static unsigned long messageCount = 0;
static bool firstMessage = true;  // DEBUG: Log first message
static const unsigned long LOG_INTERVAL_MS = 5000;  // Log summary every 5 seconds

/**
 * Message handler class - receives all N2K messages directly from the library.
 * 
 * This is the key insight: tMsgHandler with PGN=0 receives ALL messages.
 * Set a specific PGN to filter. Very lightweight - just a few bytes overhead.
 */
class HalmetN2kHandler : public tNMEA2000::tMsgHandler {
protected:
    void HandleMsg(const tN2kMsg &N2kMsg) override {
        messageCount++;
        
        // DEBUG: Log first message to confirm handler is called
        if (firstMessage && LOG) {
            LOG->logDebug(GwLog::LOG, "N2kHandler: FIRST MSG! PGN=%lu src=%d", 
                         N2kMsg.PGN, N2kMsg.Source);
            firstMessage = false;
        }
        
        unsigned long now = millis();
        if (now - lastLogTime >= LOG_INTERVAL_MS) {
            if (LOG) {
                LOG->logDebug(GwLog::LOG, "N2kHandler: %lu msgs in last %lus", 
                             messageCount, LOG_INTERVAL_MS / 1000);
            }
            messageCount = 0;
            lastLogTime = now;
        }
    }
    
public:
    /**
     * Constructor - PGN=0 means receive ALL messages
     * Pass the NMEA2000 object pointer to auto-attach
     */
    HalmetN2kHandler(tNMEA2000* nmea) 
        : tNMEA2000::tMsgHandler(0, nmea) {  // PGN=0 = all messages
    }
};

// Static handler instance - must persist for lifetime of application
static HalmetN2kHandler* handler = nullptr;

//=============================================================================
// Micro-task callbacks (run in halmetTask context AFTER NMEA2000.Open())
//=============================================================================

// Called once when micro-task first runs
static bool nmea2kHandlerInit(GwApi* api) {
    LOG = api->getLogger();
    LOG->logDebug(GwLog::LOG, "N2kHandler: Attaching to NMEA2000...");
    
    // NOW it's safe to attach - NMEA2000.Open() has been called
    handler = new HalmetN2kHandler(&NMEA2000);
    
    LOG->logDebug(GwLog::LOG, "N2kHandler: Handler attached, logging N2K messages");
    lastLogTime = millis();
    return true;  // Keep running
}

// Called periodically (does nothing - handler fires on message receipt)
static void nmea2kHandlerTask(GwApi* api) {
    // Nothing to do - HandleMsg fires automatically
}

/**
 * Init function - just registers the micro-task.
 * Runs early during boot, before NMEA2000.Open().
 */
void halmetNMEAInit(GwApi* api) {
    halmetRegisterMicroTask("N2kHandler", nmea2kHandlerTask, nmea2kHandlerInit);
}

#endif  // BOARD_HALMET
