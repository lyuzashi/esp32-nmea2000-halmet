#ifdef ENABLE_NAVLINKTASK

#pragma once
#include "GwApi.h"

void navlinkInit(GwApi *api);

// Declare the init function to be called by the core
DECLARE_INITFUNCTION(navlinkInit);

#endif // ENABLE_NAVLINKTASK