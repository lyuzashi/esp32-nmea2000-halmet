#pragma once
#include "GwApi.h"
#include "hardware.h"
#include <functional>

//we only compile for some boards
#ifdef BOARD_HALMET
#ifdef HALMET_TASK_ENABLED
//we could add the following defines also in our local platformio.ini
//CAN base 
// #define M5_CAN_KIT
//RS485 on groove
// #define SERIAL_GROOVE_485

/**
 * Micro-task callback types.
 * Small periodic functions that run in the halmetTask loop.
 * Receives the halmetTask's api pointer (valid for the task lifetime).
 */
using HalmetMicroTask = std::function<void()>;  // Legacy - no api
using HalmetMicroTaskWithApi = std::function<void(GwApi*)>;  // Periodic task - receives api
using HalmetMicroTaskInit = std::function<bool(GwApi*)>;  // One-time init - returns false to disable task

/**
 * Deferred callback type.
 * For operations that cannot be done from certain contexts (e.g., sending messages from callbacks).
 * Executed by a FreeRTOS timer after a short delay.
 */
using HalmetDeferredCallback = std::function<void()>;

/**
 * Register a micro-task to be executed periodically by halmetTask.
 * 
 * Tasks are executed sequentially with delays between them.
 * Total cycle time is fixed (default 5s), so delay between tasks
 * is dynamically calculated: cycleTime / taskCount.
 * 
 * The task receives the halmetTask's GwApi pointer, which is valid
 * for calling sendN2kMessage, increment, etc.
 * 
 * Call this during init phase (before task starts).
 * 
 * @param name  Task name for logging
 * @param task  Function to execute periodically (receives api)
 * @param init  Optional one-time init function, called on first loop iteration.
 *              Return true to continue, false to disable this micro-task.
 */
void halmetRegisterMicroTask(const char* name, HalmetMicroTaskWithApi task, 
                             HalmetMicroTaskInit init = nullptr);

/**
 * Schedule a callback to run after a short delay (1ms).
 * Use this to defer operations that cannot be done from the current context,
 * such as sending N2K messages from within a PGN callback.
 * 
 * The callback runs in the FreeRTOS timer daemon context.
 * Multiple callbacks can be queued and will execute in order.
 * 
 * @param callback  Function to execute after delay
 * @return true if successfully queued, false if queue full
 */
bool halmetDefer(HalmetDeferredCallback callback);

void halmetInit(GwApi *api);
void halmetTask(GwApi *api);

DECLARE_INITFUNCTION(halmetInit);

#endif // HALMET_TASK_ENABLED
#endif