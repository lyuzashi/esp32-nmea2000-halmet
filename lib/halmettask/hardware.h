#pragma once
#ifdef BOARD_HALMET

#define USBSerial Serial

#define ESP32_CAN_RX_PIN GPIO_NUM_18
#define ESP32_CAN_TX_PIN GPIO_NUM_19
#define DIGITAL_INPUT_1 GPIO_NUM_23

// 1-Wire bus pin for temperature sensors
#define ONEWIRE_PIN GPIO_NUM_4

#endif