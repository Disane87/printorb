#include "version.h"

#ifndef PRINTORB_VERSION
#define PRINTORB_VERSION "0.0.0-dev"
#endif

namespace Version {
    const char* STRING     = PRINTORB_VERSION;
    const char* BUILD_DATE = __DATE__ " " __TIME__;
}
