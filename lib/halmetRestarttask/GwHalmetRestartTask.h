#ifndef GW_HALMET_RESTARTTASK_H
#define GW_HALMET_RESTARTTASK_H

#include "GwApi.h"

#ifdef BOARD_HALMET
#ifdef HALMET_RESTART_COUNTER_ENABLED


void halmetRestartInit(GwApi* api);

// Run after halmet task setup and other core init handlers.
DECLARE_INITFUNCTION_ORDER(halmetRestartInit, 170);

#endif // HALMET_RESTART_COUNTER_ENABLED
#endif  // BOARD_HALMET

#endif  // GW_HALMET_RESTARTTASK_H
