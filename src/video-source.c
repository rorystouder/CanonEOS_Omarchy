#include "video-source.h"
#include "utils/logging.h"
#include "utils/error-handling.h"
#include <util/platform.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <jpeglib.h>
#include <setjmp.h>

#define FRAME_QUEUE_SIZE 4
#define MAX_FRAME_SIZE (3840 * 2160 * 4)

/**
 * @brief Frame buffer for video pipeline
 */
typedef struct {
    uint8_t *data[4];
    uint32_t linesize[4];
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;
    bool in_use;
} frame_buffer_t;

/**
 * @brief Video source implementation
 */
struct video_source_t {
    canon_camera_t *camera;
    video_format_info_t format;

    pthread_t capture_thread;
    pthread_mutex_t mutex;
    pthread_cond_t frame_available;

    frame_buffer_t frame_queue[FRAME_QUEUE_SIZE];
    int write_index;
    int read_index;
    int frame_count;

    bool active;
    bool thread_running;

    uint8_t *conversion_buffer;
    size_t conversion_buffer_size;

    uint64_t frames_captured;
    uint64_t frames_dropped;
    uint64_t last_frame_time;
};

static void *capture_thread_func(void *data);
static canon_error_t convert_jpeg_to_nv12(const uint8_t *jpeg_data, size_t jpeg_size,
                                         uint8_t *nv12_data, uint32_t *width, uint32_t *height);

video_source_t *video_source_create(void)
{
    video_source_t *source = calloc(1, sizeof(video_source_t));
    if (!source) {
        canon_log(LOG_ERROR, "Failed to allocate video source");
        return NULL;
    }

    pthread_mutex_init(&source->mutex, NULL);
    pthread_cond_init(&source->frame_available, NULL);

    source->conversion_buffer_size = MAX_FRAME_SIZE;
    source->conversion_buffer = malloc(source->conversion_buffer_size);
    if (!source->conversion_buffer) {
        canon_log(LOG_ERROR, "Failed to allocate conversion buffer");
        pthread_mutex_destroy(&source->mutex);
        pthread_cond_destroy(&source->frame_available);
        free(source);
        return NULL;
    }

    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        frame_buffer_t *frame = &source->frame_queue[i];

        frame->data[0] = malloc(MAX_FRAME_SIZE);
        if (!frame->data[0]) {
            canon_log(LOG_ERROR, "Failed to allocate frame buffer %d", i);

            for (int j = 0; j < i; j++) {
                free(source->frame_queue[j].data[0]);
            }
            free(source->conversion_buffer);
            pthread_mutex_destroy(&source->mutex);
            pthread_cond_destroy(&source->frame_available);
            free(source);
            return NULL;
        }

        frame->data[1] = NULL;
        frame->data[2] = NULL;
        frame->data[3] = NULL;
        frame->width = 0;
        frame->height = 0;
        frame->linesize[0] = 0;
        frame->linesize[1] = 0;
        frame->in_use = false;
    }

    source->format.width = 1920;
    source->format.height = 1080;
    source->format.fps = 30;
    source->format.format = VIDEO_FORMAT_NV12;
    source->format.frame_size = source->format.width * source->format.height * 3 / 2;

    return source;
}

void video_source_destroy(video_source_t *source)
{
    if (!source) {
        return;
    }

    video_source_stop(source);

    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        if (source->frame_queue[i].data[0]) {
            free(source->frame_queue[i].data[0]);
        }
    }

    if (source->conversion_buffer) {
        free(source->conversion_buffer);
    }

    pthread_cond_destroy(&source->frame_available);
    pthread_mutex_destroy(&source->mutex);

    free(source);
}

canon_error_t video_source_init(video_source_t *source,
                               canon_camera_t *camera,
                               const video_format_info_t *format)
{
    if (!source || !camera || !format) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&source->mutex);

    if (source->active) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_ERROR_CAMERA_BUSY;
    }

    source->camera = camera;
    memcpy(&source->format, format, sizeof(video_format_info_t));

    if (source->format.format == VIDEO_FORMAT_NONE) {
        source->format.format = VIDEO_FORMAT_NV12;
    }

    source->format.frame_size = source->format.width * source->format.height * 3 / 2;

    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        source->frame_queue[i].linesize[0] = source->format.width;
        source->frame_queue[i].linesize[1] = source->format.width;
        source->frame_queue[i].in_use = false;
    }

    pthread_mutex_unlock(&source->mutex);

    canon_log(LOG_INFO, "Video source initialized: %dx%d@%d",
             source->format.width, source->format.height, source->format.fps);

    return CANON_SUCCESS;
}

canon_error_t video_source_start(video_source_t *source)
{
    if (!source) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&source->mutex);

    if (source->active) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_SUCCESS;
    }

    if (!source->camera) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_ERROR_NO_DEVICE;
    }

    canon_error_t err = canon_camera_start_live_view(source->camera);
    if (err != CANON_SUCCESS) {
        pthread_mutex_unlock(&source->mutex);
        return err;
    }

    source->active = true;
    source->thread_running = true;

    if (pthread_create(&source->capture_thread, NULL, capture_thread_func, source) != 0) {
        source->active = false;
        source->thread_running = false;
        canon_camera_stop_live_view(source->camera);
        pthread_mutex_unlock(&source->mutex);
        canon_log(LOG_ERROR, "Failed to create capture thread");
        return CANON_ERROR_UNKNOWN;
    }

    pthread_mutex_unlock(&source->mutex);

    canon_log(LOG_INFO, "Video source started");
    return CANON_SUCCESS;
}

void video_source_stop(video_source_t *source)
{
    if (!source) {
        return;
    }

    pthread_mutex_lock(&source->mutex);

    if (!source->active) {
        pthread_mutex_unlock(&source->mutex);
        return;
    }

    source->active = false;
    pthread_cond_broadcast(&source->frame_available);
    pthread_mutex_unlock(&source->mutex);

    if (source->thread_running) {
        pthread_join(source->capture_thread, NULL);
        source->thread_running = false;
    }

    if (source->camera) {
        canon_camera_stop_live_view(source->camera);
    }

    canon_log(LOG_INFO, "Video source stopped");
}

bool video_source_is_active(video_source_t *source)
{
    if (!source) {
        return false;
    }

    pthread_mutex_lock(&source->mutex);
    bool active = source->active;
    pthread_mutex_unlock(&source->mutex);

    return active;
}

canon_error_t video_source_get_frame(video_source_t *source,
                                    struct obs_source_frame *frame)
{
    if (!source || !frame) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&source->mutex);

    if (!source->active) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    while (source->frame_count == 0 && source->active) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 100000000;
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000;
        }

        int ret = pthread_cond_timedwait(&source->frame_available,
                                        &source->mutex, &timeout);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&source->mutex);
            return CANON_ERROR_TIMEOUT;
        } else if (ret != 0) {
            // Handle any other error from pthread_cond_timedwait
            pthread_mutex_unlock(&source->mutex);
            canon_log(LOG_ERROR, "pthread_cond_timedwait failed with error %d", ret);
            return CANON_ERROR_UNKNOWN;
        }
    }

    if (!source->active) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    frame_buffer_t *buffer = &source->frame_queue[source->read_index];

    // Validate buffer has been properly initialized with frame data
    if (buffer->width == 0 || buffer->height == 0) {
        pthread_mutex_unlock(&source->mutex);
        canon_log(LOG_ERROR, "Buffer has invalid dimensions: %ux%u", buffer->width, buffer->height);
        return CANON_ERROR_UNKNOWN;
    }

    frame->data[0] = buffer->data[0];
    frame->data[1] = buffer->data[0] + (buffer->width * buffer->height);
    frame->linesize[0] = buffer->linesize[0];
    frame->linesize[1] = buffer->linesize[1];
    frame->timestamp = buffer->timestamp;
    frame->width = buffer->width;
    frame->height = buffer->height;
    frame->format = source->format.format;

    buffer->in_use = true;

    source->read_index = (source->read_index + 1) % FRAME_QUEUE_SIZE;
    source->frame_count--;

    pthread_mutex_unlock(&source->mutex);

    return CANON_SUCCESS;
}

void video_source_release_frame(video_source_t *source,
                               struct obs_source_frame *frame)
{
    if (!source || !frame) {
        return;
    }

    pthread_mutex_lock(&source->mutex);

    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        if (source->frame_queue[i].data[0] == frame->data[0]) {
            source->frame_queue[i].in_use = false;
            break;
        }
    }

    pthread_mutex_unlock(&source->mutex);
}

canon_error_t video_source_update_format(video_source_t *source,
                                        const video_format_info_t *format)
{
    if (!source || !format) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&source->mutex);

    if (source->active) {
        pthread_mutex_unlock(&source->mutex);
        return CANON_ERROR_CAMERA_BUSY;
    }

    memcpy(&source->format, format, sizeof(video_format_info_t));
    source->format.frame_size = source->format.width * source->format.height * 3 / 2;

    for (int i = 0; i < FRAME_QUEUE_SIZE; i++) {
        source->frame_queue[i].linesize[0] = source->format.width;
        source->frame_queue[i].linesize[1] = source->format.width;
    }

    pthread_mutex_unlock(&source->mutex);

    return CANON_SUCCESS;
}

canon_error_t video_source_get_format(video_source_t *source,
                                     video_format_info_t *format)
{
    if (!source || !format) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&source->mutex);
    memcpy(format, &source->format, sizeof(video_format_info_t));
    pthread_mutex_unlock(&source->mutex);

    return CANON_SUCCESS;
}

void video_source_get_stats(video_source_t *source,
                           uint64_t *frames_captured,
                           uint64_t *frames_dropped)
{
    if (!source) {
        return;
    }

    pthread_mutex_lock(&source->mutex);

    if (frames_captured) {
        *frames_captured = source->frames_captured;
    }

    if (frames_dropped) {
        *frames_dropped = source->frames_dropped;
    }

    pthread_mutex_unlock(&source->mutex);
}

static void *capture_thread_func(void *data)
{
    video_source_t *source = (video_source_t *)data;

    canon_log(LOG_INFO, "Capture thread started");

    while (source->thread_running && source->active) {
        size_t bytes_written = 0;
        canon_error_t err = canon_camera_capture_frame(
            source->camera,
            source->conversion_buffer,
            source->conversion_buffer_size,
            &bytes_written);

        if (err != CANON_SUCCESS) {
            if (err != CANON_ERROR_TIMEOUT) {
                canon_log(LOG_ERROR, "Failed to capture frame: %s",
                         canon_error_string(err));
            }
            usleep(1000000 / source->format.fps);
            continue;
        }

        if (source->frames_captured < 5) {
            canon_log(LOG_INFO, "Captured JPEG frame: %zu bytes", bytes_written);
        }

        pthread_mutex_lock(&source->mutex);

        if (source->frame_count >= FRAME_QUEUE_SIZE) {
            source->frames_dropped++;
            pthread_mutex_unlock(&source->mutex);
            continue;
        }

        frame_buffer_t *buffer = &source->frame_queue[source->write_index];

        if (!buffer->in_use) {
            buffer->width = source->format.width;
            buffer->height = source->format.height;

            err = convert_jpeg_to_nv12(
                source->conversion_buffer,
                bytes_written,
                buffer->data[0],
                &buffer->width,
                &buffer->height);

            if (err == CANON_SUCCESS) {
                // Update linesize to match actual dimensions
                buffer->linesize[0] = buffer->width;
                buffer->linesize[1] = buffer->width;

                buffer->timestamp = os_gettime_ns();
                source->write_index = (source->write_index + 1) % FRAME_QUEUE_SIZE;
                source->frame_count++;
                source->frames_captured++;
                source->last_frame_time = buffer->timestamp;

                if (source->frames_captured < 5) {
                    canon_log(LOG_INFO, "Converted frame to NV12: %ux%u (actual JPEG dimensions)",
                             buffer->width, buffer->height);
                }

                pthread_cond_signal(&source->frame_available);
            } else {
                canon_log(LOG_ERROR, "Failed to convert JPEG to NV12: %s",
                         canon_error_string(err));
            }
        }

        pthread_mutex_unlock(&source->mutex);

        usleep(1000000 / source->format.fps);
    }

    canon_log(LOG_INFO, "Capture thread stopped");
    return NULL;
}

static canon_error_t convert_jpeg_to_nv12(const uint8_t *jpeg_data, size_t jpeg_size,
                                         uint8_t *nv12_data, uint32_t *width, uint32_t *height)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, (unsigned char *)jpeg_data, jpeg_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        canon_log(LOG_ERROR, "Failed to read JPEG header");
        return CANON_ERROR_UNKNOWN;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    // Use actual JPEG dimensions, not requested dimensions
    uint32_t actual_width = cinfo.output_width;
    uint32_t actual_height = cinfo.output_height;

    static bool logged_mismatch = false;
    if (!logged_mismatch && (actual_width != *width || actual_height != *height)) {
        canon_log(LOG_INFO, "JPEG size: got %ux%u, requested %ux%u - using actual JPEG size",
                 actual_width, actual_height, *width, *height);
        logged_mismatch = true;
    }

    *width = actual_width;
    *height = actual_height;

    uint32_t row_stride = cinfo.output_width * cinfo.output_components;
    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    // Allocate RGB buffer
    uint8_t *rgb_data = malloc(actual_width * actual_height * 3);
    if (!rgb_data) {
        jpeg_destroy_decompress(&cinfo);
        return CANON_ERROR_MEMORY;
    }

    // Read JPEG into RGB
    uint32_t row = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(rgb_data + (row * actual_width * 3), buffer[0], actual_width * 3);
        row++;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    // Convert RGB to NV12
    uint8_t *y_plane = nv12_data;
    uint8_t *uv_plane = nv12_data + (actual_width * actual_height);

    // Process Y plane
    for (uint32_t i = 0; i < actual_height; i++) {
        for (uint32_t j = 0; j < actual_width; j++) {
            uint32_t rgb_idx = (i * actual_width + j) * 3;
            uint8_t r = rgb_data[rgb_idx];
            uint8_t g = rgb_data[rgb_idx + 1];
            uint8_t b = rgb_data[rgb_idx + 2];

            // RGB to Y
            y_plane[i * actual_width + j] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }

    // Process UV plane (subsampled 2x2)
    // NV12: UV plane is half height, half width, but U and V are interleaved
    // So each UV row has 'actual_width' bytes (actual_width/2 UV pairs * 2 bytes per pair)
    for (uint32_t i = 0; i < actual_height; i += 2) {
        for (uint32_t j = 0; j < actual_width; j += 2) {
            uint32_t rgb_idx = (i * actual_width + j) * 3;
            uint8_t r = rgb_data[rgb_idx];
            uint8_t g = rgb_data[rgb_idx + 1];
            uint8_t b = rgb_data[rgb_idx + 2];

            // UV plane index: row * actual_width + column * 2 (for interleaved UV)
            uint32_t uv_row = i / 2;
            uint32_t uv_col = j / 2;
            uint32_t uv_idx = uv_row * actual_width + uv_col * 2;

            uv_plane[uv_idx] = (uint8_t)(-0.169 * r - 0.331 * g + 0.5 * b + 128);     // U
            uv_plane[uv_idx + 1] = (uint8_t)(0.5 * r - 0.419 * g - 0.081 * b + 128);  // V
        }
    }

    free(rgb_data);
    return CANON_SUCCESS;
}