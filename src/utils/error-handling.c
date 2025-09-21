#include "error-handling.h"
#include "logging.h"
#include "../canon-errors.h"
#include <libusb-1.0/libusb.h>
#include <gphoto2/gphoto2.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

static __thread error_context_t g_last_error;
static pthread_mutex_t g_error_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* canon_error_string(canon_error_t error)
{
    switch (error) {
        case CANON_SUCCESS:
            return "Success";
        case CANON_ERROR_NO_DEVICE:
            return "No device found";
        case CANON_ERROR_USB_INIT:
            return "USB initialization failed";
        case CANON_ERROR_MEMORY:
            return "Memory allocation failed";
        case CANON_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case CANON_ERROR_CAMERA_BUSY:
            return "Camera is busy";
        case CANON_ERROR_NOT_SUPPORTED:
            return "Operation not supported";
        case CANON_ERROR_TIMEOUT:
            return "Operation timed out";
        case CANON_ERROR_DISCONNECTED:
            return "Device disconnected";
        case CANON_ERROR_PERMISSION:
            return "Permission denied";
        case CANON_ERROR_UNKNOWN:
        default:
            return "Unknown error";
    }
}

void error_set_context(error_context_t *ctx, canon_error_t code,
                      const char *func, const char *file, int line,
                      const char *fmt, ...)
{
    if (!ctx) {
        ctx = &g_last_error;
    }

    ctx->code = code;
    ctx->function = func;
    ctx->file = file;
    ctx->line = line;

    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(ctx->message, sizeof(ctx->message), fmt, args);
        va_end(args);
    } else {
        strncpy(ctx->message, canon_error_string(code),
                sizeof(ctx->message) - 1);
    }

    canon_log(LOG_ERROR, "[%s:%d] %s: %s",
             file, line, func, ctx->message);
}

const error_context_t *error_get_last(void)
{
    return &g_last_error;
}

void error_clear(void)
{
    memset(&g_last_error, 0, sizeof(error_context_t));
}

canon_error_t error_from_errno(int err)
{
    switch (err) {
        case 0:
            return CANON_SUCCESS;
        case ENOMEM:
            return CANON_ERROR_MEMORY;
        case EINVAL:
            return CANON_ERROR_INVALID_PARAM;
        case EACCES:
        case EPERM:
            return CANON_ERROR_PERMISSION;
        case ETIMEDOUT:
            return CANON_ERROR_TIMEOUT;
        case ENODEV:
            return CANON_ERROR_NO_DEVICE;
        default:
            return CANON_ERROR_UNKNOWN;
    }
}

canon_error_t error_from_usb(int err)
{
    switch (err) {
        case LIBUSB_SUCCESS:
            return CANON_SUCCESS;
        case LIBUSB_ERROR_NO_MEM:
            return CANON_ERROR_MEMORY;
        case LIBUSB_ERROR_INVALID_PARAM:
            return CANON_ERROR_INVALID_PARAM;
        case LIBUSB_ERROR_ACCESS:
            return CANON_ERROR_PERMISSION;
        case LIBUSB_ERROR_NO_DEVICE:
            return CANON_ERROR_NO_DEVICE;
        case LIBUSB_ERROR_TIMEOUT:
            return CANON_ERROR_TIMEOUT;
        case LIBUSB_ERROR_BUSY:
            return CANON_ERROR_CAMERA_BUSY;
        case LIBUSB_ERROR_NOT_SUPPORTED:
            return CANON_ERROR_NOT_SUPPORTED;
        default:
            return CANON_ERROR_UNKNOWN;
    }
}

canon_error_t error_from_gphoto(int err)
{
    if (err >= GP_OK) {
        return CANON_SUCCESS;
    }

    switch (err) {
        case GP_ERROR_NO_MEMORY:
            return CANON_ERROR_MEMORY;
        case GP_ERROR_TIMEOUT:
            return CANON_ERROR_TIMEOUT;
        case GP_ERROR_NOT_SUPPORTED:
            return CANON_ERROR_NOT_SUPPORTED;
        case GP_ERROR_BAD_PARAMETERS:
            return CANON_ERROR_INVALID_PARAM;
        case GP_ERROR_CAMERA_BUSY:
            return CANON_ERROR_CAMERA_BUSY;
        default:
            return CANON_ERROR_UNKNOWN;
    }
}