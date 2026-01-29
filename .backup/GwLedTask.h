#ifndef _GWLEDS_H
#define _GWLEDS_H

// Minimal stub for GwLedTask when excluded from build (halmet environment)
#include "GwApi.h"

// Stub init function
inline void initLeds(GwApi *param) {}

DECLARE_INITFUNCTION(initLeds);

#endif // _GWLEDS_H
