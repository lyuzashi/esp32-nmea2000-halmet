/**
 * Minimal I2C Task Runner for Halmet
 * 
 * This provides the I2C bus infrastructure (init, polling loop) without
 * any built-in sensors. Custom sensors register via api->addSensor() from
 * their own init functions.
 * 
 * This replaces iictask for halmet builds - keeps the architecture but
 * strips the sensor bloat.
 */
#ifndef _GWIICTASK_H
#define _GWIICTASK_H

#include "GwApi.h"

void initIicTask(GwApi *api);

// Declare as init function with late ordering (sensors need config ready first)
DECLARE_INITFUNCTION_ORDER(initIicTask, GWLATEORDER);

#endif
