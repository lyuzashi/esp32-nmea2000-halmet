#pragma once
#include "GwApi.h"

void loggerInit(GwApi *api);

// Declare the init function to be called by the core
DECLARE_INITFUNCTION(loggerInit);
