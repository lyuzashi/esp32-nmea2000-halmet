/**
 * Halmet Message Task
 * 
 * Provides a NMEA2000 message callback system for halmet tasks.
 * Creates a GwChannel that receives all N2K messages and allows
 * other tasks to register callbacks for specific PGNs.
 * 
 * Usage:
 *   #include "GwHalmetMessagetask.h"
 *   
 *   void myCallback(const tN2kMsg& msg) {
 *       // Handle message (called from microtask context, safe to do work)
 *   }
 *   
 *   // In your init:
 *   halmetRegisterPgnCallback(126992, myCallback);  // System Time
 */

#ifndef GW_HALMET_MESSAGETASK_H
#define GW_HALMET_MESSAGETASK_H

#include "GwApi.h"
#include "N2kMsg.h"

#ifdef BOARD_HALMET
#ifdef MESSAGE_CALLBACKS_ENABLED

/**
 * Initialize message callback channel.
 */
void halmetMessageInit(GwApi* api);

// Init before TimeTask (150 < 160)
DECLARE_INITFUNCTION_ORDER(halmetMessageInit, 150);


// Callback type for PGN handlers
typedef void (*HalmetPgnCallback)(const tN2kMsg& msg);

/**
 * Register a callback for a specific PGN.
 * Callbacks are invoked from the halmet micro-task context (not ISR).
 * Multiple callbacks can be registered for the same PGN.
 * 
 * @param pgn The PGN to listen for
 * @param callback Function to call when message arrives
 * @return true if registered successfully
 */
bool halmetRegisterPgnCallback(unsigned long pgn, HalmetPgnCallback callback);

/**
 * Unregister a callback for a specific PGN.
 * 
 * @param pgn The PGN
 * @param callback The callback to remove
 * @return true if found and removed
 */
bool halmetUnregisterPgnCallback(unsigned long pgn, HalmetPgnCallback callback);

#endif  // MESSAGE_CALLBACKS_ENABLED
#endif  // BOARD_HALMET
#endif  // GW_HALMET_MESSAGETASK_H
