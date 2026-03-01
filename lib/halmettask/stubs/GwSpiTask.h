/**
 * Stub SPI task for Halmet - provides empty implementations
 * This replaces lib/spitask when it's in lib_ignore, satisfying the include in GwUserTasks.h
 */
#ifndef _GWSPITASK_H
#define _GWSPITASK_H

#include "GwApi.h"

// Empty init function - SPI sensors not used on Halmet
inline void initSpiTask(GwApi *api) {
    // No-op: SPI sensors disabled for this build
}

// Empty run function  
inline void runSpiTask(GwApi *api) {
    // No-op: SPI sensors disabled for this build
}

#endif // _GWSPITASK_H
