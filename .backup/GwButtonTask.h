#ifndef _GWBUTTONTASK_H
#define _GWBUTTONTASK_H

// Minimal stub for GwButtonTask when excluded from build (halmet environment)
#include "GwApi.h"

// Stub init function
inline void initButtons(GwApi *param) {}
DECLARE_INITFUNCTION(initButtons);

// Minimal stub for IButtonTask interface
class IButtonTask : public GwApi::TaskInterfaces::Base
{
public:
    typedef enum
    {
        OFF,
        PRESSED,
        PRESSED_5, // 5...10s
        PRESSED_10 //>10s
    } ButtonState;
    ButtonState state=OFF;
    long pressCount=0;
};
DECLARE_TASKIF(IButtonTask);

#endif // _GWBUTTONTASK_H
