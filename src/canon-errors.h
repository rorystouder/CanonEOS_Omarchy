#ifndef CANON_ERRORS_H
#define CANON_ERRORS_H

/**
 * @brief Error codes for Canon EOS plugin operations
 */
typedef enum {
    CANON_SUCCESS = 0,
    CANON_ERROR_NO_DEVICE = -1,
    CANON_ERROR_USB_INIT = -2,
    CANON_ERROR_MEMORY = -3,
    CANON_ERROR_INVALID_PARAM = -4,
    CANON_ERROR_CAMERA_BUSY = -5,
    CANON_ERROR_NOT_SUPPORTED = -6,
    CANON_ERROR_TIMEOUT = -7,
    CANON_ERROR_DISCONNECTED = -8,
    CANON_ERROR_PERMISSION = -9,
    CANON_ERROR_UNKNOWN = -99
} canon_error_t;

/**
 * @brief Convert error code to human-readable string
 * @param error Error code
 * @return Error description string
 */
const char* canon_error_string(canon_error_t error);

#endif /* CANON_ERRORS_H */