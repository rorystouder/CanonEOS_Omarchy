#ifndef VIDEO_SOURCE_H
#define VIDEO_SOURCE_H

#include <stdint.h>
#include <stdbool.h>
#include <obs-module.h>
#include "canon-errors.h"
#include "canon-camera.h"

/**
 * @brief Video source handle
 */
typedef struct video_source_t video_source_t;

/**
 * @brief Video format information
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    enum video_format format;
    size_t frame_size;
} video_format_info_t;

/**
 * @brief Create a new video source
 * @return Video source handle or NULL on failure
 */
video_source_t *video_source_create(void);

/**
 * @brief Destroy video source
 * @param source Video source handle
 */
void video_source_destroy(video_source_t *source);

/**
 * @brief Initialize video source with camera
 * @param source Video source handle
 * @param camera Camera handle
 * @param format Video format settings
 * @return CANON_SUCCESS or error code
 */
canon_error_t video_source_init(video_source_t *source,
                               canon_camera_t *camera,
                               const video_format_info_t *format);

/**
 * @brief Start video capture
 * @param source Video source handle
 * @return CANON_SUCCESS or error code
 */
canon_error_t video_source_start(video_source_t *source);

/**
 * @brief Stop video capture
 * @param source Video source handle
 */
void video_source_stop(video_source_t *source);

/**
 * @brief Check if video source is active
 * @param source Video source handle
 * @return true if capturing
 */
bool video_source_is_active(video_source_t *source);

/**
 * @brief Get next available frame
 * @param source Video source handle
 * @param frame Output OBS frame structure
 * @return CANON_SUCCESS or error code
 */
canon_error_t video_source_get_frame(video_source_t *source,
                                    struct obs_source_frame *frame);

/**
 * @brief Release frame after use
 * @param source Video source handle
 * @param frame OBS frame structure
 */
void video_source_release_frame(video_source_t *source,
                               struct obs_source_frame *frame);

/**
 * @brief Update video format
 * @param source Video source handle
 * @param format New format settings
 * @return CANON_SUCCESS or error code
 */
canon_error_t video_source_update_format(video_source_t *source,
                                        const video_format_info_t *format);

/**
 * @brief Get current video format
 * @param source Video source handle
 * @param format Output format info
 * @return CANON_SUCCESS or error code
 */
canon_error_t video_source_get_format(video_source_t *source,
                                     video_format_info_t *format);

/**
 * @brief Get video statistics
 * @param source Video source handle
 * @param frames_captured Output total frames captured
 * @param frames_dropped Output total frames dropped
 */
void video_source_get_stats(video_source_t *source,
                           uint64_t *frames_captured,
                           uint64_t *frames_dropped);

#endif /* VIDEO_SOURCE_H */