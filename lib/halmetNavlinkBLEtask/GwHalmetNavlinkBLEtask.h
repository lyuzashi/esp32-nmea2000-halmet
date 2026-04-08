#pragma once
#ifdef BOARD_HALMET
#ifdef NAVLINK_BLE_ENABLED

#include "GwApi.h"

void navlinkBLEInit(GwApi *api);

// Init after StreamChannel (200 > 100)
DECLARE_INITFUNCTION_ORDER(navlinkBLEInit, 200);

#endif // NAVLINK_BLE_ENABLED
#endif // BOARD_HALMET