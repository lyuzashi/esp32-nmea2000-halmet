/**
 * Halmet Message Task
 * 
 * Lightweight callback-only channel for N2K message dispatch.
 * No Stream, no impl class, no buffers - just a callback.
 * 
 * Architecture:
 * - Creates minimal GwChannel with beginCallbackOnly()
 * - Channel receives sendActisense() calls from main loop
 * - Callback dispatches to registered PGN handlers
 */

#ifdef BOARD_HALMET
#ifdef MESSAGE_CALLBACKS_ENABLED

#include "GwHalmetMessagetask.h"
#include "GwHalmetTask.h"
#include "GwApi.h"
#include "GwLog.h"
#include "GwChannel.h"
#include "GwChannelList.h"
#include "N2kMsg.h"
#include <vector>

extern GwChannelList channels;

#define MSG_CHANNEL_ID 253  // Unique: 250=NavLink, 251=WS, 252=BLE

static GwLog* g_logger = nullptr;
static GwChannel* g_msgChannel = nullptr;

// Callback registry: PGN -> list of callbacks
struct PgnCallbackEntry {
    unsigned long pgn;
    HalmetPgnCallback callback;
};
static std::vector<PgnCallbackEntry> g_callbacks;

//=============================================================================
// Public API
//=============================================================================

bool halmetRegisterPgnCallback(unsigned long pgn, HalmetPgnCallback callback) {
    if (!callback) return false;
    
    g_callbacks.push_back({pgn, callback});
    
    if (g_logger) {
        g_logger->logDebug(GwLog::LOG, "HalmetMsg: registered callback for PGN %lu", pgn);
    }
    return true;
}

bool halmetUnregisterPgnCallback(unsigned long pgn, HalmetPgnCallback callback) {
    for (auto it = g_callbacks.begin(); it != g_callbacks.end(); ++it) {
        if (it->pgn == pgn && it->callback == callback) {
            g_callbacks.erase(it);
            return true;
        }
    }
    return false;
}

//=============================================================================
// N2K Callback - dispatches to PGN handlers
//=============================================================================

static void handleN2kMessage(const tN2kMsg& msg, int sourceId) {
    for (const auto& entry : g_callbacks) {
        if (entry.pgn == msg.PGN && entry.callback) {
            entry.callback(msg);
        }
    }
}

//=============================================================================
// Initialization
//=============================================================================

void halmetMessageInit(GwApi* api) {
    g_logger = api->getLogger();
    g_logger->logDebug(GwLog::LOG, "HalmetMsg: initializing");
    
    // Create lightweight callback-only channel
    g_msgChannel = new GwChannel(g_logger, "HalmetMsg", MSG_CHANNEL_ID, -1);
    g_msgChannel->beginCallbackOnly(handleN2kMessage);
    
    channels.addChannel(g_msgChannel);
    g_logger->logDebug(GwLog::LOG, "HalmetMsg: callback channel added (ID=%d)", MSG_CHANNEL_ID);
}

#endif  // MESSAGE_CALLBACKS_ENABLED
#endif  // BOARD_HALMET
