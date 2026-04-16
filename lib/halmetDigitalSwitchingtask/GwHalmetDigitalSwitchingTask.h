#pragma once
#include "GwApi.h"
#include "hardware.h"

#ifdef BOARD_HALMET
#ifdef DIGITAL_SWITCHING_ENABLED

/**
 * Initialize digital switching as a halmet microtask.
 * 
 * - Registers PGN 127502 callback for switch control commands
 * - Periodically broadcasts PGN 127501 status reports
 * - Provides placeholders for I2C GPIO input/output
 */
void halmetDigitalSwitchingInit(GwApi *api);

DECLARE_INITFUNCTION(halmetDigitalSwitchingInit);

#endif  // DIGITAL_SWITCHING_ENABLED
#endif  // BOARD_HALMET