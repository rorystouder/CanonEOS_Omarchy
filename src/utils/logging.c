#include "logging.h"
#include <time.h>
#include <sys/resource.h>

void logging_init(void)
{
    canon_log(LOG_INFO, "Logging subsystem initialized");
}

void logging_cleanup(void)
{
    canon_log(LOG_INFO, "Logging subsystem cleanup");
}

void logging_memory_stats(void)
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        canon_log(LOG_DEBUG, "Memory usage: RSS=%ld KB",
                 usage.ru_maxrss);
    }
}

void logging_performance(const char *operation, double duration_ms)
{
    if (duration_ms > 100.0) {
        canon_log(LOG_WARNING, "Slow operation '%s': %.2f ms",
                 operation, duration_ms);
    } else {
        canon_debug("Operation '%s': %.2f ms", operation, duration_ms);
    }
}