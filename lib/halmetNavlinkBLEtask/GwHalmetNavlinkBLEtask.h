#pragma once
#ifdef BOARD_HALMET
#ifdef NAVLINK_BLE_ENABLED

#include "GwApi.h"

void navlinkBLEInit(GwApi *api);
void navlinkBLETask(GwApi *api);


DECLARE_INITFUNCTION(navlinkBLEInit);

#endif // NAVLINK_BLE_ENABLED
#endif // BOARD_HALMET