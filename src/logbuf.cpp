#include "logbuf.h"
#include <stdarg.h>

namespace {
    const size_t CAP = 6000;     // bytes of scrollback kept for the web log
    char     g_buf[CAP];
    size_t   g_head = 0;
    bool     g_full = false;

    void appendStr(const char* s) {
        while (*s) {
            g_buf[g_head++] = *s++;
            if (g_head >= CAP) { g_head = 0; g_full = true; }
        }
    }
}

namespace Log {

void printf(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    Serial.print(line);          // keep the serial cable working too
    appendStr(line);
}

String dump() {
    // Fixed buffer (no realloc) keeps this safe to read from the async server
    // task while the main loop appends; a torn read at worst shows stale bytes.
    char* tmp = (char*)malloc(CAP + 1);
    if (!tmp) return String();
    size_t n = 0;
    if (g_full) for (size_t i = g_head; i < CAP; i++) tmp[n++] = g_buf[i];
    for (size_t i = 0; i < g_head; i++) tmp[n++] = g_buf[i];
    tmp[n] = 0;
    String out(tmp);
    free(tmp);
    return out;
}

}  // namespace Log
