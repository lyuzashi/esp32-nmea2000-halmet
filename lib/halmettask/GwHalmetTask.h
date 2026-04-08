#pragma once
#include "GwApi.h"
#include "hardware.h"
#include <functional>

//we only compile for some boards
#ifdef BOARD_HALMET
//we could add the following defines also in our local platformio.ini
//CAN base 
// #define M5_CAN_KIT
//RS485 on groove
// #define SERIAL_GROOVE_485

/**
 * Micro-task callback type.
 * Small periodic functions that run in the halmetTask loop.
 */
using HalmetMicroTask = std::function<void()>;

/**
 * Register a micro-task to be executed periodically by halmetTask.
 * 
 * Tasks are executed sequentially with delays between them.
 * Total cycle time is fixed (default 5s), so delay between tasks
 * is dynamically calculated: cycleTime / taskCount.
 * 
 * Call this during init phase (before task starts).
 * 
 * @param name  Task name for logging
 * @param task  Function to execute periodically
 */
void halmetRegisterMicroTask(const char* name, HalmetMicroTask task);

void halmetInit(GwApi *api);
void halmetTask(GwApi *api);


DECLARE_INITFUNCTION(halmetInit);


#endif