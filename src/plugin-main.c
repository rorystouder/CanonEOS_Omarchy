#include <obs-module.h>
#include <obs-source.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "canon-camera.h"
#include "video-source.h"
#include "camera-detector.h"
#include "utils/logging.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-canon-eos", "en-US")

#define PLUGIN_NAME "Canon EOS Camera"
#define PLUGIN_VERSION "1.0.0"

static pthread_mutex_t g_plugin_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_plugin_initialized = false;
static camera_detector_t *g_detector = NULL;

/**
 * @brief Canon EOS source structure
 */
struct canon_eos_source {
    obs_source_t *source;
    canon_camera_t *camera;
    video_source_t *video;

    pthread_t capture_thread;
    pthread_mutex_t mutex;
    bool active;
    bool thread_running;

    char *device_path;
    uint32_t width;
    uint32_t height;
    uint32_t fps;

    uint64_t frame_count;
    uint64_t last_frame_time;
};

static const char *canon_eos_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return PLUGIN_NAME;
}

static void canon_eos_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "device_path", "");
    obs_data_set_default_int(settings, "resolution", 1080);
    obs_data_set_default_int(settings, "fps", 30);
    obs_data_set_default_bool(settings, "auto_reconnect", true);
}

static obs_properties_t *canon_eos_get_properties(void *data)
{
    UNUSED_PARAMETER(data);
    obs_properties_t *props = obs_properties_create();

    obs_property_t *device_list = obs_properties_add_list(
        props, "device_path", "Camera Device",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    if (g_detector) {
        camera_info_t *cameras = NULL;
        int count = camera_detector_list_devices(g_detector, &cameras);

        obs_property_list_add_string(device_list, "None", "");

        for (int i = 0; i < count; i++) {
            char display_name[256];
            snprintf(display_name, sizeof(display_name), "%s (%s)",
                    cameras[i].model_name, cameras[i].device_path);
            obs_property_list_add_string(device_list,
                                        display_name,
                                        cameras[i].device_path);
        }

        camera_detector_free_list(cameras, count);
    }

    obs_property_t *resolution = obs_properties_add_list(
        props, "resolution", "Resolution",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

    obs_property_list_add_int(resolution, "4K (3840x2160)", 2160);
    obs_property_list_add_int(resolution, "1080p (1920x1080)", 1080);
    obs_property_list_add_int(resolution, "720p (1280x720)", 720);

    obs_properties_add_int_slider(props, "fps", "Frame Rate", 24, 60, 1);

    obs_properties_add_bool(props, "auto_reconnect", "Auto Reconnect");

    return props;
}

static void *canon_eos_capture_thread(void *data)
{
    struct canon_eos_source *source = data;
    canon_log(LOG_INFO, "Capture thread started for device: %s", source->device_path);

    while (source->thread_running) {
        pthread_mutex_lock(&source->mutex);

        if (source->active && source->camera && source->video) {
            struct obs_source_frame frame = {0};

            if (video_source_get_frame(source->video, &frame) == CANON_SUCCESS) {
                frame.timestamp = os_gettime_ns();
                frame.width = source->width;
                frame.height = source->height;
                frame.format = VIDEO_FORMAT_NV12;

                obs_source_output_video(source->source, &frame);

                source->frame_count++;
                source->last_frame_time = frame.timestamp;

                video_source_release_frame(source->video, &frame);
            }
        }

        pthread_mutex_unlock(&source->mutex);

        usleep(1000000 / source->fps);
    }

    canon_log(LOG_INFO, "Capture thread stopped");
    return NULL;
}

static void canon_eos_update(void *data, obs_data_t *settings)
{
    struct canon_eos_source *source = data;

    pthread_mutex_lock(&source->mutex);

    const char *new_device = obs_data_get_string(settings, "device_path");
    int resolution = (int)obs_data_get_int(settings, "resolution");
    source->fps = (uint32_t)obs_data_get_int(settings, "fps");

    switch (resolution) {
        case 2160:
            source->width = 3840;
            source->height = 2160;
            break;
        case 1080:
            source->width = 1920;
            source->height = 1080;
            break;
        case 720:
            source->width = 1280;
            source->height = 720;
            break;
        default:
            source->width = 1920;
            source->height = 1080;
    }

    if (!source->device_path || strcmp(source->device_path, new_device) != 0) {
        if (source->device_path) {
            bfree(source->device_path);
        }
        source->device_path = bstrdup(new_device);

        if (source->camera) {
            canon_camera_disconnect(source->camera);
            canon_camera_destroy(source->camera);
            source->camera = NULL;
        }

        if (strlen(new_device) > 0) {
            canon_config_t config = {
                .width = source->width,
                .height = source->height,
                .fps = source->fps
            };

            source->camera = canon_camera_create();
            if (source->camera) {
                canon_error_t err = canon_camera_connect(source->camera,
                                                         new_device,
                                                         &config);
                if (err != CANON_SUCCESS) {
                    canon_log(LOG_ERROR, "Failed to connect to camera: %s",
                             canon_error_string(err));
                    canon_camera_destroy(source->camera);
                    source->camera = NULL;
                }
            }
        }
    }

    pthread_mutex_unlock(&source->mutex);
}

static void *canon_eos_create(obs_data_t *settings, obs_source_t *source)
{
    struct canon_eos_source *eos = bzalloc(sizeof(struct canon_eos_source));
    eos->source = source;

    pthread_mutex_init(&eos->mutex, NULL);

    eos->video = video_source_create();
    if (!eos->video) {
        canon_log(LOG_ERROR, "Failed to create video source");
        bfree(eos);
        return NULL;
    }

    canon_eos_get_defaults(settings);
    canon_eos_update(eos, settings);

    return eos;
}

static void canon_eos_destroy(void *data)
{
    struct canon_eos_source *source = data;

    if (source->thread_running) {
        source->thread_running = false;
        pthread_join(source->capture_thread, NULL);
    }

    pthread_mutex_lock(&source->mutex);

    if (source->camera) {
        canon_camera_disconnect(source->camera);
        canon_camera_destroy(source->camera);
    }

    if (source->video) {
        video_source_destroy(source->video);
    }

    if (source->device_path) {
        bfree(source->device_path);
    }

    pthread_mutex_unlock(&source->mutex);
    pthread_mutex_destroy(&source->mutex);

    bfree(source);
}

static void canon_eos_activate(void *data)
{
    struct canon_eos_source *source = data;

    pthread_mutex_lock(&source->mutex);
    source->active = true;

    if (!source->thread_running && source->camera) {
        source->thread_running = true;
        pthread_create(&source->capture_thread, NULL,
                      canon_eos_capture_thread, source);
    }
    pthread_mutex_unlock(&source->mutex);

    canon_log(LOG_INFO, "Source activated");
}

static void canon_eos_deactivate(void *data)
{
    struct canon_eos_source *source = data;

    pthread_mutex_lock(&source->mutex);
    source->active = false;
    pthread_mutex_unlock(&source->mutex);

    canon_log(LOG_INFO, "Source deactivated");
}

static struct obs_source_info canon_eos_source = {
    .id = "canon_eos_camera_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,
    .get_name = canon_eos_get_name,
    .create = canon_eos_create,
    .destroy = canon_eos_destroy,
    .get_defaults = canon_eos_get_defaults,
    .get_properties = canon_eos_get_properties,
    .update = canon_eos_update,
    .activate = canon_eos_activate,
    .deactivate = canon_eos_deactivate,
    .icon_type = OBS_ICON_TYPE_CAMERA,
};

bool obs_module_load(void)
{
    pthread_mutex_lock(&g_plugin_mutex);

    if (g_plugin_initialized) {
        pthread_mutex_unlock(&g_plugin_mutex);
        return true;
    }

    canon_log(LOG_INFO, "Loading Canon EOS plugin v%s", PLUGIN_VERSION);

    if (canon_camera_init_library() != CANON_SUCCESS) {
        canon_log(LOG_ERROR, "Failed to initialize camera library");
        pthread_mutex_unlock(&g_plugin_mutex);
        return false;
    }

    g_detector = camera_detector_create();
    if (!g_detector) {
        canon_log(LOG_ERROR, "Failed to create camera detector");
        canon_camera_cleanup_library();
        pthread_mutex_unlock(&g_plugin_mutex);
        return false;
    }

    camera_detector_start(g_detector);

    obs_register_source(&canon_eos_source);

    g_plugin_initialized = true;
    pthread_mutex_unlock(&g_plugin_mutex);

    canon_log(LOG_INFO, "Canon EOS plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    pthread_mutex_lock(&g_plugin_mutex);

    if (!g_plugin_initialized) {
        pthread_mutex_unlock(&g_plugin_mutex);
        return;
    }

    canon_log(LOG_INFO, "Unloading Canon EOS plugin");

    if (g_detector) {
        camera_detector_stop(g_detector);
        camera_detector_destroy(g_detector);
        g_detector = NULL;
    }

    canon_camera_cleanup_library();

    g_plugin_initialized = false;
    pthread_mutex_unlock(&g_plugin_mutex);

    canon_log(LOG_INFO, "Canon EOS plugin unloaded");
}

const char *obs_module_name(void)
{
    return PLUGIN_NAME;
}

const char *obs_module_description(void)
{
    return "Enable Canon EOS cameras as video sources in OBS Studio";
}