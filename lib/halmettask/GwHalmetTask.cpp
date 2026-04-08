#ifdef BOARD_HALMET
#include "GwHalmetTask.h"
#include "GwApi.h"
#include "N2kMessages.h"
#include <vector>

// Micro-task system
// Small periodic functions registered during init, executed by halmetTask

#define HALMET_CYCLE_TIME_MS 5000  // Total cycle time for all micro-tasks

struct MicroTaskEntry {
    const char* name;
    HalmetMicroTask task;
};

static std::vector<MicroTaskEntry> g_microTasks;
static GwLog* g_logger = nullptr;

void halmetRegisterMicroTask(const char* name, HalmetMicroTask task) {
    g_microTasks.push_back({name, task});
    if (g_logger) {
        g_logger->logDebug(GwLog::LOG, "Halmet: registered micro-task '%s' (total=%d)", 
                          name, g_microTasks.size());
    }
}

void halmetTask(GwApi *api)
{    
    GwLog* logger = api->getLogger();
    logger->logDebug(GwLog::LOG, "Halmet: task started with %d micro-tasks", g_microTasks.size());
    
    while (true) {
        size_t taskCount = g_microTasks.size();
        
        if (taskCount == 0) {
            // No micro-tasks registered, just sleep
            delay(HALMET_CYCLE_TIME_MS);
        } else {
            // Calculate delay between tasks to maintain fixed cycle time
            unsigned long delayPerTask = HALMET_CYCLE_TIME_MS / taskCount;
            
            // Execute each micro-task with delay
            for (size_t i = 0; i < taskCount; i++) {
                auto& entry = g_microTasks[i];
                if (entry.task) {
                    entry.task();
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
    api->addUserTask(halmetTask, "halmetTask", 3072);
    api->addCapability("halmet", "true");
}

#endif