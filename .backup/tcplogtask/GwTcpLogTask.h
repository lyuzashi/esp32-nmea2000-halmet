#ifndef _GWTCPLOGTASK_H
#define _GWTCPLOGTASK_H


#ifdef ENABLE_TCPLOGTASK

#include "GwApi.h"

/**
 * TCP Log Task - Streams logs over TCP for remote monitoring
 * Compatible with Microsoft Serial Monitor extension using tcp://ip:port
 */
void tcpLogInit(GwApi *api);

DECLARE_INITFUNCTION(tcpLogInit);

#endif // ENABLE_TCPLOGTASK

#endif
