/**
 * Halmet Time Sync Task
 * 
 * Synchronizes ESP32 system clock from multiple sources:
 * 1. GPS via NMEA2000 PGN 126992 (System Time) - primary for boats
 * 2. NTP over WiFi - fallback for lab development with internet
 * 
 * Uses halmetMessagetask to receive PGN 126992 callbacks without
 * coupling to the core message handling.
 */

#ifdef BOARD_HALMET
#ifdef TIME_SYNC_ENABLED

#include "GwHalmetTimetask.h"
#include "GwHalmetTask.h"
#include "GwHalmetMessagetask.h"
#include "GwLog.h"
#include "N2kMessages.h"
#include <sys/time.h>
#include <time.h>
#include "esp_sntp.h"

static GwLog* g_logger = nullptr;
static GwApi* g_api = nullptr;

// Time sync state
static bool g_gpsTimeSynced = false;
static bool g_ntpTimeSynced = false;
static unsigned long g_lastGpsSyncMs = 0;
static unsigned long g_lastNtpCheckMs = 0;

// Config
static const unsigned long GPS_RESYNC_INTERVAL_MS = 3600000;   // Resync from GPS every hour
static const unsigned long NTP_CHECK_INTERVAL_MS = 60000;      // Check NTP status every minute
static const char* NTP_SERVER = "pool.ntp.org";

//=============================================================================
// GPS Time Sync (from N2K PGN 126992)
//=============================================================================

/**
 * Callback for PGN 126992 (System Time) messages.
 * Called from halmetMessagetask micro-task context.
 */
static void onSystemTime(const tN2kMsg& msg) {
    unsigned char sid;
    uint16_t daysSince1970;
    double secondsSinceMidnight;
    tN2kTimeSource timeSource;
    
    if (!ParseN2kSystemTime(msg, sid, daysSince1970, secondsSinceMidnight, timeSource)) {
        return;
    }
    
    // Basic validation
    if (daysSince1970 < 19000 || daysSince1970 > 30000) {
        // Out of reasonable range (~2022-2052)
        return;
    }
    if (secondsSinceMidnight < 0 || secondsSinceMidnight >= 86400) {
        return;
    }
    
    // Rate limit: only sync once per hour after initial sync
    unsigned long now = millis();
    if (g_gpsTimeSynced && (now - g_lastGpsSyncMs < GPS_RESYNC_INTERVAL_MS)) {
        return;
    }
    
    // Convert to Unix timestamp
    time_t gpsTime = (time_t)daysSince1970 * 86400 + (time_t)secondsSinceMidnight;
    
    // Set system time
    struct timeval tv;
    tv.tv_sec = gpsTime;
    tv.tv_usec = (suseconds_t)((secondsSinceMidnight - (int)secondsSinceMidnight) * 1000000);
    
    if (settimeofday(&tv, nullptr) == 0) {
        g_gpsTimeSynced = true;
        g_lastGpsSyncMs = now;
        
        if (g_logger) {
            struct tm timeinfo;
            gmtime_r(&gpsTime, &timeinfo);
            char timeBuf[32];
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            g_logger->logDebug(GwLog::LOG, "TimeSync: clock set to %s (GPS/N2K)", timeBuf);
        }
    }
}

//=============================================================================
// NTP Time Sync (fallback for lab/development)
//=============================================================================

static void ntpSyncCallback(struct timeval *tv) {
    g_ntpTimeSynced = true;
    
    if (g_logger) {
        time_t now = tv->tv_sec;
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        g_logger->logDebug(GwLog::LOG, "TimeSync: clock set to %s (NTP)", timeBuf);
    }
}

static void initNtp() {
    if (g_logger) {
        g_logger->logDebug(GwLog::LOG, "TimeSync: initializing NTP client");
    }
    
    // Configure SNTP - use older API for compatibility
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(ntpSyncCallback);
    sntp_init();
}

//=============================================================================
// Micro-task integration
//=============================================================================

static bool timeSyncInit(GwApi* api) {
    g_api = api;
    g_logger = api->getLogger();
    
    // Register for PGN 126992 (System Time) callbacks
    halmetRegisterPgnCallback(126992UL, onSystemTime);
    
    // Initialize NTP (works when WiFi has internet)
    initNtp();
    
    g_logger->logDebug(GwLog::LOG, "TimeSync: initialized (GPS/NTP)");
    return true;
}

static void timeSyncTask(GwApi* api) {
    unsigned long now = millis();
    
    // Periodically log sync status (every 5 minutes, debug level)
    static unsigned long lastStatusLog = 0;
    if (now - lastStatusLog > 300000) {
        lastStatusLog = now;
        
        time_t currentTime;
        time(&currentTime);
        
        // Check if time is valid (after year 2020)
        bool timeValid = (currentTime > 1577836800);
        
        if (g_logger) {
            if (timeValid) {
                struct tm timeinfo;
                gmtime_r(&currentTime, &timeinfo);
                char timeBuf[32];
                strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
                g_logger->logDebug(GwLog::DEBUG, "TimeSync: current=%s gps=%s ntp=%s",
                    timeBuf,
                    g_gpsTimeSynced ? "yes" : "no",
                    g_ntpTimeSynced ? "yes" : "no");
            } else {
                g_logger->logDebug(GwLog::DEBUG, "TimeSync: waiting for time source");
            }
        }
    }
}

// Called from DECLARE_INITFUNCTION in header
void halmetTimeInit(GwApi* api) {
    halmetRegisterMicroTask("timeSync", timeSyncTask, timeSyncInit);
}

#endif  // TIME_SYNC_ENABLED
#endif  // BOARD_HALMET
