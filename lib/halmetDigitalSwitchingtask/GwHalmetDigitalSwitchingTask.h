#pragma once
#include "GwApi.h"
#include "hardware.h"
//we only compile for some boards
#ifdef BOARD_HALMET
#ifdef DIGITAL_SWITCHING_ENABLED
//we could add the following defines also in our local platformio.ini
//CAN base 
// #define M5_CAN_KIT
//RS485 on groove
// #define SERIAL_GROOVE_485


void halmetDigitalSwitchingInit(GwApi *api);
void halmetDigitalSwitchingTask(GwApi *api);


DECLARE_INITFUNCTION(halmetDigitalSwitchingInit);

#endif
#endif