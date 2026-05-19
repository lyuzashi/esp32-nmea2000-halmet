/**
 * ADC Tank Input Sensor for Halmet
 * 
 * Reads 4 analog inputs via ADS1115 ADC on I2C.
 * First two channels can be configured as tank level sensors (PGN 127505).
 * Registers with IIC task for polling - no separate microtask.
 * 
 * Hardware: ADS1115 at address 0x4B on I2C bus
 * HALMET has 33.3/3.3 voltage divider on inputs, giving 0-33V range.
 */
#ifndef _GWHALMET_ADC_TASK_H
#define _GWHALMET_ADC_TASK_H
#ifdef BOARD_HALMET
#ifdef ADC_TANK_ENABLED

#include "GwApi.h"

void halmetADCInit(GwApi *api);

// Config items defined in Config.json:
// - tank1Enable: Enable tank 1 on channel A0
// - tank1FluidType: Fluid type (0=Fuel, 1=Water, 2=GrayWater, etc)
// - tank1Capacity: Tank capacity in liters
// - tank1EmptyV: Voltage at empty (in 0.1V units, e.g., 10 = 1.0V)
// - tank1FullV: Voltage at full (in 0.1V units, e.g., 100 = 10.0V)
// - tank2Enable, tank2FluidType, etc. for channel A1

// Init order: before IIC task so sensor is registered before IIC runs
DECLARE_INITFUNCTION_ORDER(halmetADCInit, 50);

#endif
#endif
#endif
