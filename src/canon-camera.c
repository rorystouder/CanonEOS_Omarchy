#include "canon-camera.h"
#include "utils/logging.h"
#include "utils/error-handling.h"
#include <gphoto2/gphoto2.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LIVE_VIEW_TIMEOUT_MS 5000
#define FRAME_BUFFER_COUNT 3

/**
 * @brief Canon camera implementation
 */
struct canon_camera_t {
    Camera *gphoto_camera;
    GPContext *gphoto_context;
    CameraAbilitiesList *abilities_list;
    GPPortInfoList *port_info_list;

    pthread_mutex_t mutex;
    pthread_cond_t frame_ready;

    char device_path[256];
    bool connected;
    bool live_view_active;

    canon_config_t config;
    canon_capabilities_t capabilities;

    uint8_t *frame_buffers[FRAME_BUFFER_COUNT];
    size_t frame_buffer_size;
    int current_buffer;

    uint64_t frame_count;
    uint64_t error_count;
};

static GPContext *g_gphoto_context = NULL;
static pthread_mutex_t g_library_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_library_initialized = false;

canon_error_t canon_camera_init_library(void)
{
    pthread_mutex_lock(&g_library_mutex);

    if (g_library_initialized) {
        pthread_mutex_unlock(&g_library_mutex);
        return CANON_SUCCESS;
    }

    g_gphoto_context = gp_context_new();
    if (!g_gphoto_context) {
        pthread_mutex_unlock(&g_library_mutex);
        return CANON_ERROR_MEMORY;
    }

    g_library_initialized = true;
    pthread_mutex_unlock(&g_library_mutex);

    canon_log(LOG_INFO, "Camera library initialized");
    return CANON_SUCCESS;
}

void canon_camera_cleanup_library(void)
{
    pthread_mutex_lock(&g_library_mutex);

    if (!g_library_initialized) {
        pthread_mutex_unlock(&g_library_mutex);
        return;
    }

    if (g_gphoto_context) {
        gp_context_unref(g_gphoto_context);
        g_gphoto_context = NULL;
    }

    g_library_initialized = false;
    pthread_mutex_unlock(&g_library_mutex);

    canon_log(LOG_INFO, "Camera library cleaned up");
}

canon_camera_t *canon_camera_create(void)
{
    canon_camera_t *camera = calloc(1, sizeof(canon_camera_t));
    if (!camera) {
        canon_log(LOG_ERROR, "Failed to allocate camera structure");
        return NULL;
    }

    pthread_mutex_init(&camera->mutex, NULL);
    pthread_cond_init(&camera->frame_ready, NULL);

    camera->gphoto_context = gp_context_new();
    if (!camera->gphoto_context) {
        canon_log(LOG_ERROR, "Failed to create gphoto context");
        free(camera);
        return NULL;
    }

    camera->frame_buffer_size = 1920 * 1080 * 3;
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
        camera->frame_buffers[i] = malloc(camera->frame_buffer_size);
        if (!camera->frame_buffers[i]) {
            canon_log(LOG_ERROR, "Failed to allocate frame buffer %d", i);
            for (int j = 0; j < i; j++) {
                free(camera->frame_buffers[j]);
            }
            gp_context_unref(camera->gphoto_context);
            free(camera);
            return NULL;
        }
    }

    camera->capabilities.max_width = 3840;
    camera->capabilities.max_height = 2160;
    camera->capabilities.min_fps = 24;
    camera->capabilities.max_fps = 60;
    camera->capabilities.has_live_view = true;
    camera->capabilities.has_auto_focus = true;

    return camera;
}

void canon_camera_destroy(canon_camera_t *camera)
{
    if (!camera) {
        return;
    }

    if (camera->connected) {
        canon_camera_disconnect(camera);
    }

    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
        if (camera->frame_buffers[i]) {
            free(camera->frame_buffers[i]);
        }
    }

    if (camera->gphoto_context) {
        gp_context_unref(camera->gphoto_context);
    }

    pthread_cond_destroy(&camera->frame_ready);
    pthread_mutex_destroy(&camera->mutex);

    free(camera);
}

canon_error_t canon_camera_connect(canon_camera_t *camera,
                                   const char *device_path,
                                   const canon_config_t *config)
{
    if (!camera || !device_path || !config) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_CAMERA_BUSY;
    }

    strncpy(camera->device_path, device_path, sizeof(camera->device_path) - 1);
    memcpy(&camera->config, config, sizeof(canon_config_t));

    int ret = gp_camera_new(&camera->gphoto_camera);
    if (ret < GP_OK) {
        pthread_mutex_unlock(&camera->mutex);
        canon_log(LOG_ERROR, "Failed to create camera: %s", gp_result_as_string(ret));
        return error_from_gphoto(ret);
    }

    ret = gp_abilities_list_new(&camera->abilities_list);
    if (ret < GP_OK) {
        gp_camera_unref(camera->gphoto_camera);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    ret = gp_abilities_list_load(camera->abilities_list, camera->gphoto_context);
    if (ret < GP_OK) {
        gp_abilities_list_free(camera->abilities_list);
        gp_camera_unref(camera->gphoto_camera);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    ret = gp_port_info_list_new(&camera->port_info_list);
    if (ret < GP_OK) {
        gp_abilities_list_free(camera->abilities_list);
        gp_camera_unref(camera->gphoto_camera);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    ret = gp_port_info_list_load(camera->port_info_list);
    if (ret < GP_OK) {
        gp_port_info_list_free(camera->port_info_list);
        gp_abilities_list_free(camera->abilities_list);
        gp_camera_unref(camera->gphoto_camera);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    CameraAbilities abilities;
    int model_index = gp_abilities_list_lookup_model(camera->abilities_list, "Canon");
    if (model_index >= GP_OK) {
        gp_abilities_list_get_abilities(camera->abilities_list, model_index, &abilities);
        gp_camera_set_abilities(camera->gphoto_camera, abilities);
    }

    ret = gp_camera_init(camera->gphoto_camera, camera->gphoto_context);
    if (ret < GP_OK) {
        gp_port_info_list_free(camera->port_info_list);
        gp_abilities_list_free(camera->abilities_list);
        gp_camera_unref(camera->gphoto_camera);
        pthread_mutex_unlock(&camera->mutex);
        canon_log(LOG_ERROR, "Failed to initialize camera: %s", gp_result_as_string(ret));
        return error_from_gphoto(ret);
    }

    camera->connected = true;
    pthread_mutex_unlock(&camera->mutex);

    canon_log(LOG_INFO, "Camera connected: %s", device_path);
    return CANON_SUCCESS;
}

void canon_camera_disconnect(canon_camera_t *camera)
{
    if (!camera) {
        return;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return;
    }

    if (camera->live_view_active) {
        camera->live_view_active = false;
    }

    if (camera->gphoto_camera) {
        gp_camera_exit(camera->gphoto_camera, camera->gphoto_context);
        gp_camera_unref(camera->gphoto_camera);
        camera->gphoto_camera = NULL;
    }

    if (camera->port_info_list) {
        gp_port_info_list_free(camera->port_info_list);
        camera->port_info_list = NULL;
    }

    if (camera->abilities_list) {
        gp_abilities_list_free(camera->abilities_list);
        camera->abilities_list = NULL;
    }

    camera->connected = false;
    pthread_mutex_unlock(&camera->mutex);

    canon_log(LOG_INFO, "Camera disconnected");
}

bool canon_camera_is_connected(canon_camera_t *camera)
{
    if (!camera) {
        return false;
    }

    pthread_mutex_lock(&camera->mutex);
    bool connected = camera->connected;
    pthread_mutex_unlock(&camera->mutex);

    return connected;
}

canon_error_t canon_camera_get_capabilities(canon_camera_t *camera,
                                           canon_capabilities_t *caps)
{
    if (!camera || !caps) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    memcpy(caps, &camera->capabilities, sizeof(canon_capabilities_t));

    pthread_mutex_unlock(&camera->mutex);

    return CANON_SUCCESS;
}

canon_error_t canon_camera_start_live_view(canon_camera_t *camera)
{
    if (!camera) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    if (camera->live_view_active) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_SUCCESS;
    }

    CameraWidget *config = NULL;
    CameraWidget *child = NULL;

    int ret = gp_camera_get_config(camera->gphoto_camera, &config, camera->gphoto_context);
    if (ret < GP_OK) {
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    ret = gp_widget_get_child_by_name(config, "viewfinder", &child);
    if (ret >= GP_OK) {
        int value = 1;
        gp_widget_set_value(child, &value);
        gp_camera_set_config(camera->gphoto_camera, config, camera->gphoto_context);
    }

    gp_widget_free(config);

    camera->live_view_active = true;
    pthread_mutex_unlock(&camera->mutex);

    canon_log(LOG_INFO, "Live view started");
    return CANON_SUCCESS;
}

void canon_camera_stop_live_view(canon_camera_t *camera)
{
    if (!camera) {
        return;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected || !camera->live_view_active) {
        pthread_mutex_unlock(&camera->mutex);
        return;
    }

    CameraWidget *config = NULL;
    CameraWidget *child = NULL;

    int ret = gp_camera_get_config(camera->gphoto_camera, &config, camera->gphoto_context);
    if (ret >= GP_OK) {
        ret = gp_widget_get_child_by_name(config, "viewfinder", &child);
        if (ret >= GP_OK) {
            int value = 0;
            gp_widget_set_value(child, &value);
            gp_camera_set_config(camera->gphoto_camera, config, camera->gphoto_context);
        }
        gp_widget_free(config);
    }

    camera->live_view_active = false;
    pthread_mutex_unlock(&camera->mutex);

    canon_log(LOG_INFO, "Live view stopped");
}

canon_error_t canon_camera_capture_frame(canon_camera_t *camera,
                                        uint8_t *buffer,
                                        size_t buffer_size,
                                        size_t *bytes_written)
{
    if (!camera || !buffer || !bytes_written) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    if (!camera->live_view_active) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_NOT_SUPPORTED;
    }

    CameraFile *file = NULL;
    int ret = gp_file_new(&file);
    if (ret < GP_OK) {
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    ret = gp_camera_capture_preview(camera->gphoto_camera, file, camera->gphoto_context);
    if (ret < GP_OK) {
        gp_file_unref(file);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    const char *data;
    unsigned long size;
    ret = gp_file_get_data_and_size(file, &data, &size);
    if (ret < GP_OK) {
        gp_file_unref(file);
        pthread_mutex_unlock(&camera->mutex);
        return error_from_gphoto(ret);
    }

    size_t copy_size = (size < buffer_size) ? size : buffer_size;
    memcpy(buffer, data, copy_size);
    *bytes_written = copy_size;

    gp_file_unref(file);

    camera->frame_count++;
    pthread_mutex_unlock(&camera->mutex);

    return CANON_SUCCESS;
}

canon_error_t canon_camera_set_config(canon_camera_t *camera,
                                     const canon_config_t *config)
{
    if (!camera || !config) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    memcpy(&camera->config, config, sizeof(canon_config_t));

    pthread_mutex_unlock(&camera->mutex);

    return CANON_SUCCESS;
}

canon_error_t canon_camera_get_config(canon_camera_t *camera,
                                     canon_config_t *config)
{
    if (!camera || !config) {
        return CANON_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&camera->mutex);

    if (!camera->connected) {
        pthread_mutex_unlock(&camera->mutex);
        return CANON_ERROR_DISCONNECTED;
    }

    memcpy(config, &camera->config, sizeof(canon_config_t));

    pthread_mutex_unlock(&camera->mutex);

    return CANON_SUCCESS;
}