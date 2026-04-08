/**
 * Halmet Stream Channel
 * 
 * Shared channel for streaming NMEA2000 messages in NavLink Blue format.
 * Multiple listeners (BLE, WebSocket, etc.) can register callbacks.
 */
#ifndef _GWHALMETSTREAMCHANNELTASK_H
#define _GWHALMETSTREAMCHANNELTASK_H

#include "GwApi.h"
#include <functional>

#ifdef BOARD_HALMET

// Callback type for receiving formatted PDGY messages
using HalmetStreamCallback = std::function<void(const char* msg, size_t len)>;

// Callback type for periodic cleanup (WS cleanupClients, BLE health check, etc.)
using HalmetCleanupCallback = std::function<void()>;

// Listener handle for unregistering
using HalmetStreamHandle = int;

/**
 * Interface for registering/unregistering stream listeners and cleanup callbacks.
 * 
 * BIDIRECTIONAL DESIGN NOTES:
 * 
 * OUTGOING (N2K → Listeners):
 *   - main.cpp calls sendActisense() on all channels
 *   - Our channel receives Actisense binary, parses via ActisenseReader
 *   - Converts to PDGY text and dispatches to all listeners (WS, BLE)
 *   - Counter: "Stream" with sub-keys per listener ("WS", "BLE")
 * 
 * INCOMING (Listeners → N2K):
 *   - WS/BLE receives PDGY text from client
 *   - Calls processIncoming() which parses PDGY → tN2kMsg
 *   - Distributes to OTHER stream listeners (echo prevention via handle)
 *   - Calls handleN2kMessage() with our channel's sourceId (250)
 *   - Counter: "StreamRx" with sub-keys per listener
 * 
 * ECHO PREVENTION:
 *   When a message arrives from a listener (e.g., WS client A):
 *   1. We distribute directly to OTHER listeners (WS client B, BLE) - sender excluded
 *   2. We call handleN2kMessage() with STREAM_CHANNEL_ID (250) as sourceId
 *   3. GwChannel::sendActisense() checks sourceId and skips our channel (match!)
 *   4. Result: Message goes to N2K bus + other channels, but NOT back to our listeners
 *   
 *   This two-phase approach ensures:
 *   - Other stream clients get the message immediately (direct dispatch)
 *   - N2K bus and other channels get it via handleN2kMessage
 *   - No echo back to the sender
 *   - No duplicate delivery to other stream listeners
 */
class HalmetStreamInterface {
public:
    virtual ~HalmetStreamInterface() {}
    
    // Register a listener with name (for counter display), returns handle for unregistering
    virtual HalmetStreamHandle addListener(const char* name, HalmetStreamCallback callback) = 0;
    
    // Remove a listener by handle
    virtual void removeListener(HalmetStreamHandle handle) = 0;
    
    // Get count of active listeners
    virtual size_t listenerCount() const = 0;
    
    // Register periodic cleanup callback (called every ~5s from task loop)
    virtual void addCleanupCallback(HalmetCleanupCallback callback) = 0;
    
    /**
     * Process an incoming PDGY message from a listener.
     * 
     * 1. Distributes to OTHER stream listeners (excluding sender via fromHandle)
     * 2. Calls handleN2kMessage() with our sourceId to inject into N2K system
     * 3. GwChannel skip logic prevents echo back to our channel
     * 
     * @param listenerName  Name of the listener ("WS", "BLE") for counter tracking
     * @param pdgyLine      PDGY message: !PDGY,<pgn>,<prio>,<src>,<dst>,<timer>,<base64>
     * @param fromHandle    Handle of sending listener (excluded from distribution)
     * @return true if message was valid and processed, false on parse error
     */
    virtual bool processIncoming(const char* listenerName, const char* pdgyLine, HalmetStreamHandle fromHandle) = 0;
};

// Global accessor for the shared stream interface (set during init)
HalmetStreamInterface* getHalmetStreamInterface();

void halmetStreamInit(GwApi* api);

// Init early (100) so WS/BLE can register callbacks in their init
DECLARE_INITFUNCTION_ORDER(halmetStreamInit, 100);

#endif  // BOARD_HALMET

#endif  // _GWHALMETSTREAMCHANNELTASK_H
