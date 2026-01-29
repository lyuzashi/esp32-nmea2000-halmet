#ifdef BOARD_HALMET
#include "GwHalmetTask.h"
#include "GwApi.h"



void halmetTask(GwApi *api)
{    
    while (true)
    {
        delay(2000); // Check every 2 seconds
    }
    vTaskDelete(NULL);
}


void halmetInit(GwApi *api)
{
    api->addUserTask(halmetTask, "halmetTask", 1000);
}
#endif