/**
 * Halmet Time Sync Task
 * 
 * Synchronizes ESP32 system clock from multiple sources:
 * 1. GPS via NMEA2000 PGN 126992 (System Time) - primary for boats
 * 2. NTP over WiFi - fallback for lab development with internet
 * 
 * Hooks into handleN2kMessage() via weak symbol to receive PGN 126992.
 * Registers as a halmet micro-task.
 */
#ifndef GW_HALMET_TIMETASK_H
#define GW_HALMET_TIMETASK_H

#include "GwApi.h"

#ifdef BOARD_HALMET
#ifdef TIME_SYNC_ENABLED

/**
 * Initialize time sync task.
 * Called during halmet init via DECLARE_INITFUNCTION.
 */
void halmetTimeInit(GwApi* api);

// Init after MessageTask (160 > 150)
DECLARE_INITFUNCTION_ORDER(halmetTimeInit, 160);

#endif  // TIME_SYNC_ENABLED
#endif  // BOARD_HALMET

#endif  // GW_HALMET_TIMETASK_H
