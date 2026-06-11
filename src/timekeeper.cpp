#include "timekeeper.h"
#include "logbuf.h"
#include <time.h>

namespace Time {

void begin(const String& posixTz) {
    // configTzTime applies the POSIX TZ (DST rules) and kicks off SNTP. The
    // ESP-IDF SNTP client resyncs periodically on its own afterwards.
    const char* tz = posixTz.length() ? posixTz.c_str() : "UTC0";
    configTzTime(tz, "pool.ntp.org", "time.nist.gov");
    Log::printf("[Time] SNTP start tz=%s\n", tz);
}

bool synced() {
    // Anything past 2023-11 means SNTP has set the clock (boot starts at epoch 0).
    return time(nullptr) > 1700000000;
}

int localMinutes() {
    if (!synced()) return -1;
    struct tm t;
    if (!getLocalTime(&t, 0)) return -1;
    return t.tm_hour * 60 + t.tm_min;
}

}  // namespace Time
