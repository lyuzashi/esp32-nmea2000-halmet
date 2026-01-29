#pragma once
#include "GwApi.h"
//we only compile for some boards
#ifdef BOARD_HALMET
//we could add the following defines also in our local platformio.ini
//CAN base 
// #define M5_CAN_KIT
//RS485 on groove
// #define SERIAL_GROOVE_485


#define USBSerial Serial

// from HALMET
// CAN bus (NMEA 2000) pins on HALMET
// const gpio_num_t kCANRxPin = GPIO_NUM_18;
// const gpio_num_t kCANTxPin = GPIO_NUM_19;


#define ESP32_CAN_RX_PIN GPIO_NUM_18
#define ESP32_CAN_TX_PIN GPIO_NUM_19
#define DIGITAL_INPUT_1 GPIO_NUM_23

void halmetInit(GwApi *api);
void halmetTask(GwApi *api);


DECLARE_INITFUNCTION(halmetInit);


#endif