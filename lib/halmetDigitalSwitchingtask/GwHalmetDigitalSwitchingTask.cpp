/**
 * Halmet Digital Switching Task
 * 
 * Implements NMEA 2000 digital switching (PGN 127501/127502):
 * - Stores switch bank state in tN2kBinaryStatus
 * - Responds to PGN 127502 (Switch Bank Control) via callback
 * - Periodically broadcasts PGN 127501 (Binary Status Report)
 * - Placeholder hooks for I2C GPIO input/output
 * 
 * Architecture:
 * - Uses halmet microtask for periodic status broadcast
 * - Uses halmetRegisterPgnCallback for receiving control messages
 * - State changes trigger immediate response (127501 with deltas)
 */

#ifdef BOARD_HALMET
#ifdef DIGITAL_SWITCHING_ENABLED

#include "GwHalmetTask.h"
#include "GwHalmetMessagetask.h"
#include "GwApi.h"
#include "GwLog.h"
#include "N2kMessages.h"
#include <vector>

// Configuration
#define NUM_SWITCH_BANKS 1          // Number of banks to manage
#define SWITCHES_PER_BANK 28        // Max switches per bank (N2K limit)
#define STATUS_BROADCAST_MS 10000    // Broadcast full status every 10s
#define OUR_BANK_INSTANCE 0         // Bank instance we manage

static GwLog* g_logger = nullptr;
static GwApi* g_api = nullptr;

// Switch bank state - one tN2kBinaryStatus per bank instance
// tN2kBinaryStatus is uint64_t with 2 bits per switch (28 switches max)
static tN2kBinaryStatus g_bankStatus[NUM_SWITCH_BANKS];

// Timing for periodic broadcast
static unsigned long g_lastBroadcast = 0;

//=============================================================================
// I2C GPIO Placeholders
//=============================================================================

/**
 * Placeholder: Read physical switch states from I2C GPIO expander.
 * 
 * @param bankInstance  Which bank to read
 * @param status        Output: tN2kBinaryStatus with physical input states
 * @return true if read successful, false if hardware error
 * 
 * TODO: Implement I2C communication to read GPIO inputs.
 * Example hardware: MCP23017, PCF8574, etc.
 */
static bool readPhysicalInputs(unsigned char bankInstance, tN2kBinaryStatus& status) {
    // Placeholder - not implemented yet
    // When implemented:
    // - Read GPIO expander via I2C
    // - Map GPIO states to tN2kOnOff values
    // - Set N2kOnOff_On/N2kOnOff_Off for each physical switch
    return false;  // Return false = no hardware present
}

/**
 * Placeholder: Write physical outputs to I2C GPIO expander.
 * 
 * @param bankInstance  Which bank to write
 * @param switchIndex   Switch index (1-28)
 * @param state         N2kOnOff_On or N2kOnOff_Off
 * @return true if write successful, false if hardware error
 * 
 * NOTE: This runs in deferred context (timer daemon), safe for I2C operations.
 * TODO: Implement I2C communication to control GPIO outputs.
 * Example hardware: MCP23017, PCF8574, relay boards, etc.
 */
static bool writePhysicalOutput(unsigned char bankInstance, uint8_t switchIndex, tN2kOnOff state) {
    // Log the state change
    if (g_logger) {
        g_logger->logDebug(GwLog::DEBUG, "DigSwitch: Bank %d Sw%d -> %s",
                          bankInstance, switchIndex,
                          state == N2kOnOff_On ? "ON" : "OFF");
    }
    
    // TODO: Implement I2C communication to control GPIO outputs
    // Return false to reject a switch change (e.g., hardware failure)
    return true;
}

//=============================================================================
// PGN 127502 Handler - Switch Bank Control
//=============================================================================

// Change record for deferred physical writes
struct SwitchChange {
    uint8_t sw;
    tN2kOnOff state;
};

/**
 * Handle incoming PGN 127502 (Switch Bank Control) messages.
 * 
 * Updates our internal state and sends response.
 */
static void onSwitchControl(const tN2kMsg& msg) {
    unsigned char targetBank;
    tN2kBinaryStatus controlStatus;
    
    if (!ParseN2kSwitchbankControl(msg, targetBank, controlStatus)) {
        return;  // Parse failed
    }


    
    // Check if this is for a bank we manage
    if (targetBank >= NUM_SWITCH_BANKS) {
        return;  // Not our bank
    }
    
    // Track which switches actually changed for the response
    tN2kBinaryStatus responseStatus;
    N2kResetBinaryStatus(responseStatus);  // All unavailable = no change
    bool anyChanged = false;
    
    // Collect changes to apply
    SwitchChange changes[SWITCHES_PER_BANK];
    int changeCount = 0;
    
    // Process each switch in the control message
    for (uint8_t sw = 1; sw <= SWITCHES_PER_BANK; sw++) {
        tN2kOnOff commanded = N2kGetStatusOnBinaryStatus(controlStatus, sw);
        
        // Only process On or Off commands (not Unavailable/Error)
        if (commanded == N2kOnOff_On || commanded == N2kOnOff_Off) {
            tN2kOnOff current = N2kGetStatusOnBinaryStatus(g_bankStatus[targetBank], sw);
            
            // Record change if different
            if (commanded != current) {
                // Update stored state immediately
                N2kSetStatusBinaryOnStatus(g_bankStatus[targetBank], commanded, sw);
                N2kSetStatusBinaryOnStatus(responseStatus, commanded, sw);
                
                // Queue for deferred physical write
                changes[changeCount++] = {sw, commanded};
                anyChanged = true;
            }
        }
    }
    
    // Defer physical writes and response (runs in timer daemon context)
    if (anyChanged && g_api) {
        unsigned char bank = targetBank;
        tN2kBinaryStatus status = g_bankStatus[targetBank];
        
        // Copy changes for the lambda
        std::vector<SwitchChange> deferredChanges(changes, changes + changeCount);
        
        halmetDefer([bank, status, deferredChanges]() {
            // Execute physical writes
            for (const auto& change : deferredChanges) {
                writePhysicalOutput(bank, change.sw, change.state);
            }
            
            // Send response
            tN2kMsg response;
            SetN2kBinaryStatus(response, bank, status);
            g_api->sendN2kMessage(response);
        });
    }
}

//=============================================================================
// Microtask - Periodic Status Broadcast
//=============================================================================

/**
 * Microtask: Periodically broadcast full switch bank status.
 * Also reads physical inputs if hardware is present.
 */
static void digitalSwitchingTask(GwApi* api) {
    unsigned long now = millis();
    
    // Check if it's time to broadcast
    if (now - g_lastBroadcast < STATUS_BROADCAST_MS) {
        return;
    }
    g_lastBroadcast = now;
    
    // Read physical inputs (placeholder - updates g_bankStatus if hardware present)
    for (unsigned char bank = 0; bank < NUM_SWITCH_BANKS; bank++) {
        tN2kBinaryStatus physicalInputs;
        if (readPhysicalInputs(bank, physicalInputs)) {
            // Merge physical inputs into our state
            // (Only update switches that have physical inputs mapped)
            // For now, this is a no-op since readPhysicalInputs returns false
        }
    }
    
    // Broadcast full status for all banks
    for (unsigned char bank = 0; bank < NUM_SWITCH_BANKS; bank++) {
        tN2kMsg msg;
        SetN2kBinaryStatus(msg, bank, g_bankStatus[bank]);
        api->sendN2kMessage(msg);
    }
}

/**
 * One-time initialization for digital switching.
 */
static bool digitalSwitchingInit(GwApi* api) {
    g_logger = api->getLogger();
    g_api = api;
    
    g_logger->logDebug(GwLog::LOG, "DigSwitch: Initializing %d banks", NUM_SWITCH_BANKS);
    
    // Initialize all banks to Unavailable (unknown state)
    for (int bank = 0; bank < NUM_SWITCH_BANKS; bank++) {
        N2kResetBinaryStatus(g_bankStatus[bank]);
    }
    
    // Register callback for PGN 127502 (Switch Bank Control)
    halmetRegisterPgnCallback(127502, onSwitchControl);
    
    g_logger->logDebug(GwLog::LOG, "DigSwitch: Registered PGN 127502 callback");
    
    return true;
}

//=============================================================================
// Public Init Function
//=============================================================================

void halmetDigitalSwitchingInit(GwApi* api) {
    halmetRegisterMicroTask("digitalSwitching", digitalSwitchingTask, digitalSwitchingInit);
}

#endif  // DIGITAL_SWITCHING_ENABLED
#endif  // BOARD_HALMET