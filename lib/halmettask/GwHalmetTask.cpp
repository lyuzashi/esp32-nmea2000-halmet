#ifdef BOARD_HALMET
#include "GwHalmetTask.h"
#include "GwApi.h"
#include "N2kMessages.h"
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

// Micro-task system
// Small periodic functions registered during init, executed by halmetTask

#define HALMET_CYCLE_TIME_MS 5000  // Total cycle time for all micro-tasks

struct MicroTaskEntry {
    const char* name;
    HalmetMicroTaskWithApi task;  // Takes api parameter
    HalmetMicroTaskInit init;     // Optional one-time init
    bool initialized;             // Has init been called?
    bool enabled;                 // Did init succeed (or no init needed)?
};

// Use pointer + lazy init to avoid static initialization order issues
static std::vector<MicroTaskEntry>* g_microTasks = nullptr;
static GwLog* g_logger = nullptr;

static std::vector<MicroTaskEntry>& getMicroTasks() {
    if (!g_microTasks) {
        g_microTasks = new std::vector<MicroTaskEntry>();
    }
    return *g_microTasks;
}

void halmetRegisterMicroTask(const char* name, HalmetMicroTaskWithApi task,
                             HalmetMicroTaskInit init) {
    getMicroTasks().push_back({name, task, init, false, true});
    if (g_logger) {
        g_logger->logDebug(GwLog::LOG, "Halmet: registered micro-task '%s' (total=%d)", 
                          name, getMicroTasks().size());
    }
}

//=============================================================================
// Deferred Callback System
//=============================================================================

#define HALMET_DEFER_QUEUE_SIZE 8

static TimerHandle_t g_deferTimer = nullptr;
static QueueHandle_t g_deferQueue = nullptr;

// Timer callback - processes all queued callbacks
static void deferTimerCallback(TimerHandle_t xTimer) {
    HalmetDeferredCallback* callback;
    while (xQueueReceive(g_deferQueue, &callback, 0) == pdTRUE) {
        if (callback) {
            (*callback)();
            delete callback;
        }
    }
}

bool halmetDefer(HalmetDeferredCallback callback) {
    if (!g_deferQueue || !g_deferTimer) {
        return false;
    }
    
    // Allocate callback on heap (will be deleted after execution)
    auto* cb = new HalmetDeferredCallback(std::move(callback));
    
    if (xQueueSend(g_deferQueue, &cb, 0) != pdTRUE) {
        delete cb;
        return false;  // Queue full
    }
    
    // Start/restart timer (1ms one-shot)
    xTimerStart(g_deferTimer, 0);
    return true;
}

static void initDeferredSystem() {
    g_deferQueue = xQueueCreate(HALMET_DEFER_QUEUE_SIZE, sizeof(HalmetDeferredCallback*));
    g_deferTimer = xTimerCreate("halmetDefer", pdMS_TO_TICKS(1), pdFALSE, nullptr, deferTimerCallback);
    
    if (!g_deferQueue || !g_deferTimer) {
        if (g_logger) {
            g_logger->logDebug(GwLog::ERROR, "Halmet: Failed to create deferred system");
        }
    }
}

//=============================================================================

void halmetTask(GwApi *api)
{    
    GwLog* logger = api->getLogger();
    auto& tasks = getMicroTasks();
    logger->logDebug(GwLog::LOG, "Halmet: task started with %d micro-tasks", tasks.size());
    
    while (true) {
        size_t taskCount = tasks.size();
        
        if (taskCount == 0) {
            // No micro-tasks registered, just sleep
            delay(HALMET_CYCLE_TIME_MS);
        } else {
            // Calculate delay between tasks to maintain fixed cycle time
            unsigned long delayPerTask = HALMET_CYCLE_TIME_MS / taskCount;
            
            // Execute each micro-task with delay, passing the task's api
            for (size_t i = 0; i < taskCount; i++) {
                auto& entry = tasks[i];
                
                // Run one-time init on first iteration
                if (!entry.initialized) {
                    entry.initialized = true;
                    if (entry.init) {
                        entry.enabled = entry.init(api);
                        if (!entry.enabled) {
                            logger->logDebug(GwLog::LOG, "Halmet: micro-task '%s' disabled by init", entry.name);
                        }
                    }
                }
                
                // Run periodic task if enabled
                if (entry.enabled && entry.task) {
                    entry.task(api);
                }
                delay(delayPerTask);
            }
        }
    }

    vTaskDelete(NULL);
}

void halmetInit(GwApi *api)
{
    g_logger = api->getLogger();
    initDeferredSystem();
    api->addUserTask(halmetTask, "halmetTask", 3072);
    api->addCapability("halmet", "true");
}

#endif