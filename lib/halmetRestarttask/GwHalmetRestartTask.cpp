#ifdef BOARD_HALMET
#ifdef HALMET_RESTART_COUNTER_ENABLED

#include "GwHalmetRestartTask.h"
#include "GwHalmetTask.h"

#include <Preferences.h>
#include <esp_system.h>

static GwLog* g_logger = nullptr;
static int g_counterId = -1;

static void incrementBy(GwApi* api, int counterId, const char* key, uint32_t value) {
    for (uint32_t i = 0; i < value; i++) {
        api->increment(counterId, key);
    }
}

static const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

static bool restartInit(GwApi* api) {
    g_logger = api->getLogger();
    g_counterId = api->addCounter("restarts");
    const char* reason = resetReasonToString(esp_reset_reason());

    // Persist long-term totals across resets in NVS.
    Preferences prefs;
    uint32_t total = 0;
    uint32_t reasonTotal = 0;
    if (prefs.begin("halmet", false)) {
        total = prefs.getULong("restartTotal", 0);
        total++;
        prefs.putULong("restartTotal", total);

        String reasonKey = String("rr_") + reason;
        reasonTotal = prefs.getULong(reasonKey.c_str(), 0);
        reasonTotal++;
        prefs.putULong(reasonKey.c_str(), reasonTotal);

        prefs.end();
    }

    // Mirror only the persisted total into counter UI.
    // Counter API only supports +1 increments, so we replay the value at boot.
    if (g_counterId >= 0) {
        incrementBy(api, g_counterId, "total", total);
    }

    // Export persisted values so they can be queried from status/capabilities.
    api->addCapability("restartTotal", String(total));
    api->addCapability("restartReason", String(reason));
    api->addCapability("restartReasonTotal", String(reasonTotal));

    if (g_logger) {
        g_logger->logDebug(
            GwLog::LOG,
            "RestartMon: reason=%s total=%lu reasonTotal=%lu",
            reason,
            static_cast<unsigned long>(total),
            static_cast<unsigned long>(reasonTotal)
        );
    }

    return true;
}

static void restartTask(GwApi* api) {
    (void)api;
    // Intentionally empty: this micro-task uses init to record restart data.
}

void halmetRestartInit(GwApi* api) {
    halmetRegisterMicroTask("restartMon", restartTask, restartInit);
}

#endif  // HALMET_RESTART_COUNTER_ENABLED
#endif  // BOARD_HALMET
