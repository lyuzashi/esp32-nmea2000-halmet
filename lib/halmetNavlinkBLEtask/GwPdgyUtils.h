/**
 * PDGY (NavLink Blue) Format Utilities
 * 
 * Shared conversion functions for PDGY ↔ tN2kMsg.
 * PDGY format: !PDGY,<pgn>,<priority>,<source>,<dest>,<timer>,<base64data>
 * 
 * Example: !PDGY,127250,2,22,255,12345.678,AAABBCC==
 */
#ifndef _GWPDGYUTILS_H
#define _GWPDGYUTILS_H

#include "N2kMsg.h"
#include <base64.h>
#include <mbedtls/base64.h>

/**
 * Format tN2kMsg to PDGY string.
 * 
 * @param msg     Source N2K message
 * @param buf     Output buffer (must be at least 320 bytes for max N2K message)
 * @param bufLen  Size of output buffer
 * @return        Length of formatted string, or -1 on error
 */
inline int n2kMsgToPdgy(const tN2kMsg& msg, char* buf, size_t bufLen) {
    if (!buf || bufLen < 64) return -1;
    
    // Timer: seconds with 1ms resolution, wraps at 100000s
    float timer = (millis() % 100000000) / 1000.0f;
    
    // Base64 encode the data payload
    String encoded = base64::encode(msg.Data, msg.DataLen);
    
    int len = snprintf(buf, bufLen, 
        "!PDGY,%lu,%d,%d,%d,%.3f,%s",
        msg.PGN, msg.Priority, msg.Source, msg.Destination, 
        timer, encoded.c_str());
    
    return (len > 0 && len < (int)bufLen) ? len : -1;
}

/**
 * Parse PDGY string to tN2kMsg.
 * 
 * @param pdgyLine  Input PDGY string (with or without trailing CRLF)
 * @param msg       Output N2K message
 * @return          true on success, false on parse error
 */
inline bool pdgyToN2kMsg(const char* pdgyLine, tN2kMsg& msg) {
    if (!pdgyLine) return false;
    
    // Check prefix
    if (strncmp(pdgyLine, "!PDGY,", 6) != 0) {
        return false;
    }
    const char* p = pdgyLine + 6;
    
    // Parse fields
    unsigned long pgn;
    int priority, source, dest;
    float timer;
    char base64Data[320];  // Max base64 for 223 bytes = ~300 chars
    
    int parsed = sscanf(p, "%lu,%d,%d,%d,%f,%319[^\r\n]",
                       &pgn, &priority, &source, &dest, &timer, base64Data);
    
    if (parsed < 6) {
        return false;
    }
    
    // Base64 decode
    uint8_t decodedBuf[224];
    size_t decodedLen = 0;
    int ret = mbedtls_base64_decode(decodedBuf, sizeof(decodedBuf), &decodedLen,
                                    (const unsigned char*)base64Data, strlen(base64Data));
    if (ret != 0 || decodedLen == 0 || decodedLen > 223) {
        return false;
    }
    
    // Build tN2kMsg
    msg.Init(priority, pgn, source, dest);
    
    for (size_t i = 0; i < decodedLen; i++) {
        msg.AddByte(decodedBuf[i]);
    }
    
    return true;
}

#endif  // _GWPDGYUTILS_H
