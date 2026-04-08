#ifndef _GWBME280SENSORtask_H
#define _GWBME280SENSORtask_H

#include "GwApi.h"

#ifdef BME280_ENABLED  // Only if BME280 support is enabled

void bme280TaskInit(GwApi *api);

DECLARE_INITFUNCTION(bme280TaskInit);

#endif  // BME280_ENABLED

#endif  // _GWBME280SENSORTASK_H
