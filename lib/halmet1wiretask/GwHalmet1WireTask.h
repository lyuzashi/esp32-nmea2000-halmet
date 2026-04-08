#ifndef _GWHALMET1WIRETASK_H
#define _GWHALMET1WIRETASK_H

#include "GwApi.h"

#ifdef ONEWIRE_ENABLED

void oneWireInit(GwApi *api);

DECLARE_INITFUNCTION(oneWireInit);

#endif  // ONEWIRE_ENABLED

#endif  // _GWHALMET1WIRETASK_H
