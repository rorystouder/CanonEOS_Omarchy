#ifndef CANON_CAMERA_H
#define CANON_CAMERA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "canon-errors.h"

/**
 * @brief Camera configuration
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    bool auto_focus;
    bool live_view;
} canon_config_t;

/**
 * @brief Camera handle
 */
typedef struct canon_camera_t canon_camera_t;

/**
 * @brief Camera capabilities
 */
typedef struct {
    uint32_t max_width;
    uint32_t max_height;
    uint32_t min_fps;
    uint32_t max_fps;
    bool has_live_view;
    bool has_auto_focus;
} canon_capabilities_t;

/**
 * @brief Initialize camera library (call once at startup)
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_init_library(void);

/**
 * @brief Cleanup camera library (call once at shutdown)
 */
void canon_camera_cleanup_library(void);

/**
 * @brief Create a new camera instance
 * @return Camera handle or NULL on failure
 */
canon_camera_t *canon_camera_create(void);

/**
 * @brief Destroy camera instance
 * @param camera Camera handle
 */
void canon_camera_destroy(canon_camera_t *camera);

/**
 * @brief Connect to a camera
 * @param camera Camera handle
 * @param device_path USB device path
 * @param config Initial configuration
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_connect(canon_camera_t *camera,
                                   const char *device_path,
                                   const canon_config_t *config);

/**
 * @brief Disconnect from camera
 * @param camera Camera handle
 */
void canon_camera_disconnect(canon_camera_t *camera);

/**
 * @brief Check if camera is connected
 * @param camera Camera handle
 * @return true if connected
 */
bool canon_camera_is_connected(canon_camera_t *camera);

/**
 * @brief Get camera capabilities
 * @param camera Camera handle
 * @param caps Output capabilities
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_get_capabilities(canon_camera_t *camera,
                                           canon_capabilities_t *caps);

/**
 * @brief Start live view mode
 * @param camera Camera handle
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_start_live_view(canon_camera_t *camera);

/**
 * @brief Stop live view mode
 * @param camera Camera handle
 */
void canon_camera_stop_live_view(canon_camera_t *camera);

/**
 * @brief Capture a frame from live view
 * @param camera Camera handle
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param bytes_written Actual bytes written
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_capture_frame(canon_camera_t *camera,
                                        uint8_t *buffer,
                                        size_t buffer_size,
                                        size_t *bytes_written);

/**
 * @brief Set camera configuration
 * @param camera Camera handle
 * @param config New configuration
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_set_config(canon_camera_t *camera,
                                     const canon_config_t *config);

/**
 * @brief Get current camera configuration
 * @param camera Camera handle
 * @param config Output configuration
 * @return CANON_SUCCESS or error code
 */
canon_error_t canon_camera_get_config(canon_camera_t *camera,
                                     canon_config_t *config);

#endif /* CANON_CAMERA_H */