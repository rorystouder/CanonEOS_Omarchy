#ifndef UTILS_ERROR_HANDLING_H
#define UTILS_ERROR_HANDLING_H

#include "../canon-errors.h"
#include <errno.h>

/**
 * @brief Check and handle memory allocation
 */
#define ALLOC_CHECK(ptr) \
    do { \
        if (!(ptr)) { \
            canon_log(LOG_ERROR, "Memory allocation failed at %s:%d", \
                     __FILE__, __LINE__); \
            return CANON_ERROR_MEMORY; \
        } \
    } while(0)

/**
 * @brief Check and handle memory allocation (void return)
 */
#define ALLOC_CHECK_VOID(ptr) \
    do { \
        if (!(ptr)) { \
            canon_log(LOG_ERROR, "Memory allocation failed at %s:%d", \
                     __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

/**
 * @brief Check and handle memory allocation (NULL return)
 */
#define ALLOC_CHECK_NULL(ptr) \
    do { \
        if (!(ptr)) { \
            canon_log(LOG_ERROR, "Memory allocation failed at %s:%d", \
                     __FILE__, __LINE__); \
            return NULL; \
        } \
    } while(0)

/**
 * @brief Safe free macro
 */
#define SAFE_FREE(ptr) \
    do { \
        if (ptr) { \
            free(ptr); \
            ptr = NULL; \
        } \
    } while(0)

/**
 * @brief Error context for detailed reporting
 */
typedef struct {
    canon_error_t code;
    const char *function;
    const char *file;
    int line;
    char message[256];
} error_context_t;

/**
 * @brief Set error context
 * @param ctx Error context
 * @param code Error code
 * @param func Function name
 * @param file File name
 * @param line Line number
 * @param fmt Format string
 */
void error_set_context(error_context_t *ctx, canon_error_t code,
                      const char *func, const char *file, int line,
                      const char *fmt, ...);

/**
 * @brief Get last error context
 * @return Last error context
 */
const error_context_t *error_get_last(void);

/**
 * @brief Clear error context
 */
void error_clear(void);

/**
 * @brief Convert system errno to canon error
 * @param err System error number
 * @return Canon error code
 */
canon_error_t error_from_errno(int err);

/**
 * @brief Convert libusb error to canon error
 * @param err LibUSB error code
 * @return Canon error code
 */
canon_error_t error_from_usb(int err);

/**
 * @brief Convert gphoto2 error to canon error
 * @param err GPhoto2 error code
 * @return Canon error code
 */
canon_error_t error_from_gphoto(int err);

#endif /* UTILS_ERROR_HANDLING_H */