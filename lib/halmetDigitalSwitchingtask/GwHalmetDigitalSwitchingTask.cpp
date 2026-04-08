#ifdef BOARD_HALMET
#ifdef DIGITAL_SWITCHING_ENABLED

#include "GwHalmetTask.h"
#include "GwApi.h"

#include "N2kMessages.h"


void halmetDigitalSwitchingTask(GwApi *api)
{    
    GwLog* logger = api->getLogger();
    
    const int NUM_SWITCHES = 20;
    int iteration = 0;
    int loopCount = 0;
    tN2kMsg msg;
    
    // Cumulative state tracking - persists across iterations
    tN2kBinaryStatus cumulativeStatus;
    N2kResetBinaryStatus(cumulativeStatus);  // Start all unavailable
    // Initialize switches 1-20 as Off
    for (int sw = 1; sw <= NUM_SWITCHES; sw++) {
        N2kSetStatusBinaryOnStatus(cumulativeStatus, N2kOnOff_Off, sw);
    }

    while (true)
    {
        // Determine which switch to change and its new state
        int currentSwitch;
        bool turnOn;
        
        if (iteration < NUM_SWITCHES) {
            // Phase 1: turning on from left to right
            currentSwitch = iteration + 1;
            turnOn = true;
        } else {
            // Phase 2: turning off from left to right  
            currentSwitch = iteration - NUM_SWITCHES + 1;
            turnOn = false;
        }
        
        // Update cumulative state
        N2kSetStatusBinaryOnStatus(cumulativeStatus, turnOn ? N2kOnOff_On : N2kOnOff_Off, currentSwitch);
        
        // Send control message with ONLY this switch, rest unavailable
        tN2kBinaryStatus singleSwitchStatus;
        N2kResetBinaryStatus(singleSwitchStatus);
        N2kSetStatusBinaryOnStatus(singleSwitchStatus, turnOn ? N2kOnOff_On : N2kOnOff_Off, currentSwitch);
        
        SetN2kSwitchbankControl(msg, 1, singleSwitchStatus);
        api->sendN2kMessage(msg);

        // Simulate responding with updated swtich state
        delay(50);
        SetN2kBinaryStatus(msg, 1, singleSwitchStatus);
        api->sendN2kMessage(msg);

        loopCount++;
        
        // Every 10 loops, send full cumulative status
        if (loopCount % 10 == 0) {
            SetN2kBinaryStatus(msg, 1, cumulativeStatus);
            api->sendN2kMessage(msg);
        }
        
        // Next iteration (0-39, then wrap)
        iteration = (iteration + 1) % (NUM_SWITCHES * 2);
        
        delay(200);
    }

    vTaskDelete(NULL);
}


void halmetDigitalSwitchingInit(GwApi *api)
{
    api->addUserTask(halmetDigitalSwitchingTask, "halmetDigitalSwitchingTask", 3072);

}
#endif
#endif