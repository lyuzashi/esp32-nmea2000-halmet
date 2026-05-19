/**
 * Halmet NMEA Task - Proof of Concept
 * 
 * Direct hook into NMEA2000 library using tMsgHandler.
 * This bypasses GwChannel entirely - zero heap overhead for buffers.
 * 
 * Uses tNMEA2000::tMsgHandler which auto-attaches to the NMEA2000 object
 * and receives messages directly in the ParseMessages() context.
 */

#ifndef GW_HALMET_NMEATASK_H
#define GW_HALMET_NMEATASK_H

#include "GwApi.h"

#ifdef BOARD_HALMET
#ifdef HALMET_NMEA_CALLBACK_ENABLED

/**
 * Initialize the NMEA message handler.
 * Must be called after NMEA2000.Open() completes.
 */
void halmetNMEAInit(GwApi* api);

// Init order 50 - run early to debug
DECLARE_INITFUNCTION_ORDER(halmetNMEAInit, 50);

#endif  // HALMET_NMEA_CALLBACK_ENABLED
#endif  // BOARD_HALMET

#endif  // GW_HALMET_NMEATASK_H
