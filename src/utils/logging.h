#ifndef UTILS_LOGGING_H
#define UTILS_LOGGING_H

#include <obs-module.h>

/**
 * @brief Log levels matching OBS
 */
#define LOG_ERROR   LOG_ERROR
#define LOG_WARNING LOG_WARNING
#define LOG_INFO    LOG_INFO
#define LOG_DEBUG   LOG_DEBUG

/**
 * @brief Plugin-specific logging macro
 */
#define canon_log(level, format, ...) \
    blog(level, "[Canon-EOS] " format, ##__VA_ARGS__)

/**
 * @brief Debug logging (only in debug builds)
 */
#ifdef DEBUG_MODE
    #define canon_debug(format, ...) \
        canon_log(LOG_DEBUG, format, ##__VA_ARGS__)
#else
    #define canon_debug(format, ...) ((void)0)
#endif

/**
 * @brief Initialize logging subsystem
 */
void logging_init(void);

/**
 * @brief Cleanup logging subsystem
 */
void logging_cleanup(void);

/**
 * @brief Log memory usage statistics
 */
void logging_memory_stats(void);

/**
 * @brief Log performance metrics
 * @param operation Operation name
 * @param duration_ms Duration in milliseconds
 */
void logging_performance(const char *operation, double duration_ms);

#endif /* UTILS_LOGGING_H */