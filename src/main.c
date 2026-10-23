#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "usb_device_uvc.h"
#include "camera_pins.h"

static const char *TAG = "usb_camera";
enum { UVC_BUFFER_BYTES = 192 * 1024 };

typedef struct {
    int width;
    int height;
    int rate;
} camera_mode_t;

// Use the same sdkconfig values as the USB descriptors. Changing menuconfig
// therefore changes both what the PC sees and what the sensor captures.
static const camera_mode_t s_modes[] = {
    {CONFIG_UVC_CAM1_FRAMESIZE_WIDTH, CONFIG_UVC_CAM1_FRAMESIZE_HEIGT,
     CONFIG_UVC_CAM1_FRAMERATE},
#if CONFIG_UVC_CAM1_MULTI_FRAMESIZE
    {CONFIG_UVC_MULTI_FRAME_WIDTH_1, CONFIG_UVC_MULTI_FRAME_HEIGHT_1,
     CONFIG_UVC_MULTI_FRAME_FPS_1},
    {CONFIG_UVC_MULTI_FRAME_WIDTH_2, CONFIG_UVC_MULTI_FRAME_HEIGHT_2,
     CONFIG_UVC_MULTI_FRAME_FPS_2},
    {CONFIG_UVC_MULTI_FRAME_WIDTH_3, CONFIG_UVC_MULTI_FRAME_HEIGHT_3,
     CONFIG_UVC_MULTI_FRAME_FPS_3},
#endif
};

static atomic_int s_requested_mode = 0;
// Camera initialization/reconfiguration runs only in the capture task after
// startup. The USB control task requests a mode without touching frame buffers.
static int s_active_mode = -1;

// The UVC component copies each frame into its transfer buffer before returning
// it. Keep the camera buffer owned until fb_return_cb is called.
static camera_fb_t *s_camera_frame;
static uvc_fb_t s_usb_frame;

static esp_err_t init_camera(const camera_mode_t *mode)
{
    framesize_t frame_size = FRAMESIZE_INVALID;
    for (int i = 0; i < FRAMESIZE_INVALID; ++i) {
        if (resolution[i].width == mode->width && resolution[i].height == mode->height) {
            frame_size = (framesize_t)i;
            break;
        }
    }
    if (frame_size == FRAMESIZE_INVALID) {
        ESP_LOGE(TAG, "Unsupported sensor resolution: %dx%d", mode->width, mode->height);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d0 = CAM_PIN_D0,
        .pin_d1 = CAM_PIN_D1,
        .pin_d2 = CAM_PIN_D2,
        .pin_d3 = CAM_PIN_D3,
        .pin_d4 = CAM_PIN_D4,
        .pin_d5 = CAM_PIN_D5,
        .pin_d6 = CAM_PIN_D6,
        .pin_d7 = CAM_PIN_D7,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        // OV2640's nominal 24 MHz input clock raises the sensor capture rate.
        .xclk_freq_hz = 24000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = frame_size,
        // Lower numbers give better JPEG quality, at the cost of USB bandwidth.
        .jpeg_quality = mode->width >= 1280 ? 14 : (mode->width <= 320 ? 10 : 12),
        .fb_count = 3,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .jpeg_buffer_size = UVC_BUFFER_BYTES,
    };
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s. Check the ribbon cable and pin map.",
                 esp_err_to_name(err));
        return err;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL || sensor->id.PID != OV2640_PID) {
        ESP_LOGE(TAG, "Expected an OV2640 sensor");
        esp_camera_deinit();
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sensor->set_vflip(sensor, CAM_VFLIP) != 0 ||
        sensor->set_hmirror(sensor, CAM_HMIRROR) != 0) {
        ESP_LOGE(TAG, "Failed to configure camera orientation");
        esp_camera_deinit();
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t stream_start(uvc_format_t format, int width, int height,
                              int rate, void *ctx)
{
    (void)ctx;
    if (format == UVC_FORMAT_JPEG) {
        for (size_t i = 0; i < sizeof(s_modes) / sizeof(s_modes[0]); ++i) {
            const camera_mode_t *mode = &s_modes[i];
            if (width == mode->width && height == mode->height && rate == mode->rate) {
                atomic_store(&s_requested_mode, (int)i);
                ESP_LOGI(TAG, "Stream requested: MJPEG %dx%d@%d", width, height, rate);
                return ESP_OK;
            }
        }
    }
    ESP_LOGE(TAG, "Unsupported USB mode: format=%d %dx%d@%d", format, width, height, rate);
    return ESP_ERR_NOT_SUPPORTED;
}

static void stream_stop(void *ctx)
{
    (void)ctx;
    // Keep the sensor initialized: USB suspend/reopen must not race capture.
    ESP_LOGI(TAG, "USB stream stopped/suspended");
}

static uvc_fb_t *frame_get(void *ctx)
{
    (void)ctx;
    const int requested = atomic_load(&s_requested_mode);
    const camera_mode_t *mode = &s_modes[requested];
    if (s_active_mode != requested) {
        if (s_active_mode >= 0) {
            esp_camera_deinit();
            s_active_mode = -1;
        }
        if (init_camera(mode) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
            return NULL;
        }
        s_active_mode = requested;
    }

    s_camera_frame = esp_camera_fb_get();
    if (s_camera_frame == NULL) {
        ESP_LOGW(TAG, "Camera capture timed out");
        return NULL;
    }
    // Discard a capture if the host requested a different mode while it was in
    // progress. Reinitialize on the next call with no outstanding camera frame.
    if (atomic_load(&s_requested_mode) != requested ||
        s_camera_frame->format != PIXFORMAT_JPEG ||
        s_camera_frame->width != mode->width || s_camera_frame->height != mode->height ||
        s_camera_frame->len == 0 || s_camera_frame->len > UVC_BUFFER_BYTES) {
        ESP_LOGW(TAG, "Dropping invalid/oversized frame: %zu bytes", s_camera_frame->len);
        esp_camera_fb_return(s_camera_frame);
        s_camera_frame = NULL;
        return NULL;
    }
    s_usb_frame = (uvc_fb_t) {
        .buf = s_camera_frame->buf,
        .len = s_camera_frame->len,
        .width = s_camera_frame->width,
        .height = s_camera_frame->height,
        // pixformat_t and uvc_format_t have different enum values.
        .format = UVC_FORMAT_JPEG,
        .timestamp = s_camera_frame->timestamp,
    };
    return &s_usb_frame;
}

static void frame_return(uvc_fb_t *frame, void *ctx)
{
    (void)ctx;
    if (frame == &s_usb_frame && s_camera_frame != NULL) {
        esp_camera_fb_return(s_camera_frame);
        s_camera_frame = NULL;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "GOOUUU ESP32-S3-CAM / OV2640 / USB UVC");
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM unavailable. N16R8 requires octal PSRAM enabled.");
        return;
    }
    ESP_LOGI(TAG, "PSRAM: %zu bytes", esp_psram_get_size());
    // Verify capture before enumerating a USB camera that might have no image.
    uvc_fb_t *first_frame = frame_get(NULL);
    if (first_frame == NULL) {
        ESP_LOGE(TAG, "Initial capture failed; USB camera was not started");
        if (s_active_mode >= 0) {
            esp_camera_deinit();
        }
        return;
    }
    ESP_LOGI(TAG, "First JPEG captured: %zux%zu, %zu bytes",
             first_frame->width, first_frame->height, first_frame->len);
    frame_return(first_frame, NULL);

    uint8_t *buffer = heap_caps_malloc(UVC_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot allocate USB frame buffer");
        esp_camera_deinit();
        return;
    }
    uvc_device_config_t usb = {
        .uvc_buffer = buffer,
        .uvc_buffer_size = UVC_BUFFER_BYTES,
        .start_cb = stream_start,
        .stop_cb = stream_stop,
        .fb_get_cb = frame_get,
        .fb_return_cb = frame_return,
        .cb_ctx = NULL,
    };
    ESP_ERROR_CHECK(uvc_device_config(0, &usb));
    ESP_ERROR_CHECK(uvc_device_init());
    for (size_t i = 0; i < sizeof(s_modes) / sizeof(s_modes[0]); ++i) {
        ESP_LOGI(TAG, "USB mode: MJPEG %dx%d, target %d fps",
                 s_modes[i].width, s_modes[i].height, s_modes[i].rate);
    }
    ESP_LOGI(TAG, "USB camera ready. Select MJPEG and a resolution in your webcam viewer.");
}
