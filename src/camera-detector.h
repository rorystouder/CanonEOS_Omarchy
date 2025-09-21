#ifndef CAMERA_DETECTOR_H
#define CAMERA_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>
#include "canon-errors.h"

/**
 * @brief Camera information structure
 */
typedef struct camera_info_t {
    char device_path[256];
    char model_name[128];
    char serial_number[64];
    uint16_t vendor_id;
    uint16_t product_id;
    bool is_supported;
} camera_info_t;

/**
 * @brief Camera detector handle
 */
typedef struct camera_detector_t camera_detector_t;

/**
 * @brief Callback for camera connection events
 */
typedef void (*camera_event_callback)(const camera_info_t *info, bool connected, void *user_data);

/**
 * @brief Create a new camera detector instance
 * @return Detector handle or NULL on failure
 */
camera_detector_t *camera_detector_create(void);

/**
 * @brief Destroy camera detector instance
 * @param detector Detector handle
 */
void camera_detector_destroy(camera_detector_t *detector);

/**
 * @brief Start monitoring for camera connections
 * @param detector Detector handle
 * @return CANON_SUCCESS or error code
 */
canon_error_t camera_detector_start(camera_detector_t *detector);

/**
 * @brief Stop monitoring for camera connections
 * @param detector Detector handle
 */
void camera_detector_stop(camera_detector_t *detector);

/**
 * @brief List currently connected cameras
 * @param detector Detector handle
 * @param cameras Output array (caller must free with camera_detector_free_list)
 * @return Number of cameras found
 */
int camera_detector_list_devices(camera_detector_t *detector, camera_info_t **cameras);

/**
 * @brief Free camera list
 * @param cameras Camera array
 * @param count Number of cameras
 */
void camera_detector_free_list(camera_info_t *cameras, int count);

/**
 * @brief Register callback for camera events
 * @param detector Detector handle
 * @param callback Event callback function
 * @param user_data User data for callback
 */
void camera_detector_set_callback(camera_detector_t *detector,
                                  camera_event_callback callback,
                                  void *user_data);

/**
 * @brief Check if a camera model is supported
 * @param vendor_id USB vendor ID
 * @param product_id USB product ID
 * @return true if supported, false otherwise
 */
bool camera_detector_is_supported(uint16_t vendor_id, uint16_t product_id);

#endif /* CAMERA_DETECTOR_H */