/**
 * Wrapper for GwConfigDefinitions that adds stub constants
 * 
 * When "converter" category is filtered, those constants are removed
 * from the generated GwConfigDefinitions.h. This wrapper includes
 * the generated file and adds back the constants main.cpp needs.
 */
#ifndef _GWCONFIGDEFINITIONS_H
#define _GWCONFIGDEFINITIONS_H

// First, include the real generated definitions with a different guard
#define _GWCONFIGDEFINITIONS_H_GENERATED
#include "../../generated/GwConfigDefinitions.h"
#undef _GWCONFIGDEFINITIONS_H_GENERATED

// Now check if converter constants exist, if not define them
// These will be used by main.cpp - the values won't be in config,
// so getString/getBool will return the defaults
#ifndef GWCONFIG_HAS_TALKERID
namespace GwConverterConfigStubs {
    static constexpr const char* talkerId = "talkerId";
    static constexpr const char* sendN2k = "sendN2k";
}
// Add these to GwConfigDefinitions via a derived class trick
#endif

#endif
