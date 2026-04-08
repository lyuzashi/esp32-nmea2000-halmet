#ifndef _GWHALMETSSESTREAMTASK_H
#define _GWHALMETSSESTREAMTASK_H

#include "GwApi.h"

#ifdef BOARD_HALMET
#ifdef SSE_STREAM_ENABLED

void sseStreamInit(GwApi *api);

DECLARE_INITFUNCTION(sseStreamInit);

#endif  // SSE_STREAM_ENABLED
#endif  // BOARD_HALMET

#endif  // _GWHALMETSSESTREAMTASK_H
