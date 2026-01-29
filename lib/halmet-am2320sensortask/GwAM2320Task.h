#ifndef _GWAM2320SENSORTASK_H
#define _GWAM2320SENSORTASK_H
#ifdef AM2320_ENABLED  // Only if AM2320 support is enabled

#include "GwApi.h"

void am2320TaskInit(GwApi *api);

// Declare this as a user init function
DECLARE_INITFUNCTION(am2320TaskInit);

#endif

#endif  // AM2320_ENABLED