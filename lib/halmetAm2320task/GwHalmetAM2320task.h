#ifndef _GWAM2320SENSORTASK_H
#define _GWAM2320SENSORTASK_H

#include "GwApi.h"

#ifdef AM2320_ENABLED  // Only if AM2320 support is enabled

void am2320TaskInit(GwApi *api);

// Declare this as a user init function
DECLARE_INITFUNCTION(am2320TaskInit);

#endif  // AM2320_ENABLED

#endif  // _GWAM2320SENSORTASK_H