#ifndef _GWHALMETWSSTREAMTASK_H
#define _GWHALMETWSSTREAMTASK_H

#include "GwApi.h"

#ifdef BOARD_HALMET
#ifdef WS_STREAM_ENABLED

void wsStreamInit(GwApi *api);

// Init after StreamChannel (200 > 100)
DECLARE_INITFUNCTION_ORDER(wsStreamInit, 200);

#endif  // WS_STREAM_ENABLED
#endif  // BOARD_HALMET

#endif  // _GWHALMETWSSTREAMTASK_H
