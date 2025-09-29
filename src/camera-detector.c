#include "camera-detector.h"
#include "utils/logging.h"
#include "utils/error-handling.h"
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CANON_VENDOR_ID 0x04A9
#define MAX_CAMERAS 16
#define POLL_INTERVAL_MS 1000

/**
 * @brief Canon camera model database
 */
typedef struct {
    uint16_t product_id;
    const char *model_name;
} canon_model_t;

static const canon_model_t g_supported_models[] = {
    {0x3264, "Canon EOS 5D Mark III"},
    {0x3265, "Canon EOS 5D Mark IV"},
    {0x326F, "Canon EOS 6D"},
    {0x3270, "Canon EOS 6D Mark II"},
    {0x3252, "Canon EOS 7D Mark II"},
    {0x32D1, "Canon EOS R"},
    {0x32D2, "Canon EOS R5"},
    {0x32D3, "Canon EOS R6"},
    {0x3280, "Canon EOS 90D"},
    {0x3299, "Canon EOS M50 Mark II"},
    {0, NULL}
};

/**
 * @brief Camera detector structure
 */
struct camera_detector_t {
    libusb_context *usb_context;
    libusb_hotplug_callback_handle hotplug_handle;
    
    pthread_t monitor_thread;
    pthread_mutex_t mutex;
    bool running;
    
    camera_info_t cameras[MAX_CAMERAS];
    int camera_count;
    
    camera_event_callback event_callback;
    void *callback_user_data;
};

static const char *get_model_name(uint16_t product_id)
{
    for (int i = 0; g_supported_models[i].model_name != NULL; i++) {
        if (g_supported_models[i].product_id == product_id) {
            return g_supported_models[i].model_name;
        }
    }
    return "Unknown Canon Camera";
}

bool camera_detector_is_supported(uint16_t vendor_id, uint16_t product_id)
{
    if (vendor_id != CANON_VENDOR_ID) {
        return false;
    }
    
    for (int i = 0; g_supported_models[i].model_name != NULL; i++) {
        if (g_supported_models[i].product_id == product_id) {
            return true;
        }
    }
    return false;
}

static int hotplug_callback(libusb_context *ctx, libusb_device *device,
                          libusb_hotplug_event event, void *user_data)
{
    UNUSED_PARAMETER(ctx);
    camera_detector_t *detector = (camera_detector_t *)user_data;
    struct libusb_device_descriptor desc;
    
    if (libusb_get_device_descriptor(device, &desc) != 0) {
        return 0;
    }
    
    if (desc.idVendor != CANON_VENDOR_ID) {
        return 0;
    }
    
    pthread_mutex_lock(&detector->mutex);
    
    camera_info_t info = {0};
    info.vendor_id = desc.idVendor;
    info.product_id = desc.idProduct;
    info.is_supported = camera_detector_is_supported(desc.idVendor, desc.idProduct);
    
    snprintf(info.model_name, sizeof(info.model_name), "%s",
            get_model_name(desc.idProduct));
    
    uint8_t bus = libusb_get_bus_number(device);
    uint8_t addr = libusb_get_device_address(device);
    snprintf(info.device_path, sizeof(info.device_path),
            "/dev/bus/usb/%03d/%03d", bus, addr);
    
    if (desc.iSerialNumber) {
        libusb_device_handle *handle;
        if (libusb_open(device, &handle) == 0) {
            libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber,
                                              (unsigned char *)info.serial_number,
                                              sizeof(info.serial_number));
            libusb_close(handle);
        }
    }
    
    bool connected = (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
    
    if (connected) {
        bool found = false;
        for (int i = 0; i < detector->camera_count; i++) {
            if (strcmp(detector->cameras[i].device_path, info.device_path) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found && detector->camera_count < MAX_CAMERAS) {
            memcpy(&detector->cameras[detector->camera_count], &info, sizeof(camera_info_t));
            detector->camera_count++;
            
            canon_log(LOG_INFO, "Camera connected: %s at %s",
                     info.model_name, info.device_path);
        }
    } else {
        for (int i = 0; i < detector->camera_count; i++) {
            if (strcmp(detector->cameras[i].device_path, info.device_path) == 0) {
                canon_log(LOG_INFO, "Camera disconnected: %s", info.model_name);
                
                memmove(&detector->cameras[i], &detector->cameras[i + 1],
                       (detector->camera_count - i - 1) * sizeof(camera_info_t));
                detector->camera_count--;
                break;
            }
        }
    }
    
    if (detector->event_callback) {
        detector->event_callback(&info, connected, detector->callback_user_data);
    }
    
    pthread_mutex_unlock(&detector->mutex);
    
    return 0;
}

static void *monitor_thread_func(void *data)
{
    camera_detector_t *detector = (camera_detector_t *)data;

    canon_log(LOG_DEBUG, "Camera monitor thread started");

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout

    while (detector->running) {
        libusb_handle_events_timeout_completed(detector->usb_context, &tv, NULL);
    }

    canon_log(LOG_DEBUG, "Camera monitor thread stopped");
    return NULL;
}

camera_detector_t *camera_detector_create(void)
{
    camera_detector_t *detector = calloc(1, sizeof(camera_detector_t));
    if (!detector) {
        canon_log(LOG_ERROR, "Failed to allocate camera detector");
        return NULL;
    }
    
    if (libusb_init(&detector->usb_context) != 0) {
        canon_log(LOG_ERROR, "Failed to initialize libusb");
        free(detector);
        return NULL;
    }
    
    pthread_mutex_init(&detector->mutex, NULL);
    
    libusb_device **devices;
    ssize_t count = libusb_get_device_list(detector->usb_context, &devices);
    
    if (count > 0) {
        for (ssize_t i = 0; i < count; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devices[i], &desc) == 0) {
                if (desc.idVendor == CANON_VENDOR_ID &&
                    detector->camera_count < MAX_CAMERAS) {
                    
                    camera_info_t *info = &detector->cameras[detector->camera_count];
                    info->vendor_id = desc.idVendor;
                    info->product_id = desc.idProduct;
                    info->is_supported = camera_detector_is_supported(desc.idVendor, desc.idProduct);
                    
                    snprintf(info->model_name, sizeof(info->model_name), "%s",
                            get_model_name(desc.idProduct));
                    
                    uint8_t bus = libusb_get_bus_number(devices[i]);
                    uint8_t addr = libusb_get_device_address(devices[i]);
                    snprintf(info->device_path, sizeof(info->device_path),
                            "/dev/bus/usb/%03d/%03d", bus, addr);
                    
                    detector->camera_count++;
                    
                    canon_log(LOG_INFO, "Found camera: %s at %s",
                             info->model_name, info->device_path);
                }
            }
        }
        libusb_free_device_list(devices, 1);
    }
    
    return detector;
}

void camera_detector_destroy(camera_detector_t *detector)
{
    if (!detector) {
        return;
    }
    
    camera_detector_stop(detector);
    
    pthread_mutex_destroy(&detector->mutex);
    
    if (detector->usb_context) {
        libusb_exit(detector->usb_context);
    }
    
    free(detector);
}

canon_error_t camera_detector_start(camera_detector_t *detector)
{
    if (!detector) {
        return CANON_ERROR_INVALID_PARAM;
    }
    
    if (detector->running) {
        return CANON_SUCCESS;
    }
    
    int rc = libusb_hotplug_register_callback(
        detector->usb_context,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_ENUMERATE,
        CANON_VENDOR_ID,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_callback,
        detector,
        &detector->hotplug_handle);
    
    if (rc != LIBUSB_SUCCESS) {
        canon_log(LOG_ERROR, "Failed to register hotplug callback: %s",
                 libusb_strerror(rc));
        return CANON_ERROR_USB_INIT;
    }
    
    detector->running = true;
    
    if (pthread_create(&detector->monitor_thread, NULL,
                      monitor_thread_func, detector) != 0) {
        canon_log(LOG_ERROR, "Failed to create monitor thread");
        detector->running = false;
        libusb_hotplug_deregister_callback(detector->usb_context,
                                          detector->hotplug_handle);
        return CANON_ERROR_UNKNOWN;
    }
    
    canon_log(LOG_INFO, "Camera detector started");
    return CANON_SUCCESS;
}

void camera_detector_stop(camera_detector_t *detector)
{
    if (!detector || !detector->running) {
        return;
    }
    
    detector->running = false;
    
    pthread_join(detector->monitor_thread, NULL);
    
    libusb_hotplug_deregister_callback(detector->usb_context,
                                      detector->hotplug_handle);
    
    canon_log(LOG_INFO, "Camera detector stopped");
}

int camera_detector_list_devices(camera_detector_t *detector, camera_info_t **cameras)
{
    if (!detector || !cameras) {
        return 0;
    }
    
    pthread_mutex_lock(&detector->mutex);
    
    if (detector->camera_count > 0) {
        *cameras = calloc(detector->camera_count, sizeof(camera_info_t));
        if (*cameras) {
            memcpy(*cameras, detector->cameras,
                  detector->camera_count * sizeof(camera_info_t));
        }
    }
    
    int count = detector->camera_count;
    pthread_mutex_unlock(&detector->mutex);
    
    return count;
}

void camera_detector_free_list(camera_info_t *cameras, int count)
{
    UNUSED_PARAMETER(count);
    if (cameras) {
        free(cameras);
    }
}

void camera_detector_set_callback(camera_detector_t *detector,
                                 camera_event_callback callback,
                                 void *user_data)
{
    if (!detector) {
        return;
    }
    
    pthread_mutex_lock(&detector->mutex);
    detector->event_callback = callback;
    detector->callback_user_data = user_data;
    pthread_mutex_unlock(&detector->mutex);
}