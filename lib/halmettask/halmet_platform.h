#pragma once
// Include required headers which are missing for PIOArduino platform

#if __has_include("esp_app_desc.h")
#include "esp_app_desc.h"
#endif

#if __has_include("esp_mac.h")
#include "esp_mac.h"
#endif
