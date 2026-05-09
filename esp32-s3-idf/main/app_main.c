#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "max30102.h"
#include "signal_processing.h"
#include "telemetry.h"
#include "wifi_http.h"

typedef struct {
    bool valid;
    int systolic;
    int diastolic;
    float baseline_bpm;
    float baseline_pi;
    uint32_t calibrated_at_ms;
} calibration_state_t;

static const char *TAG = "bp_monitor";

static QueueHandle_t sample_queue;
static QueueHandle_t telemetry_queue;
static SemaphoreHandle_t state_mutex;

static ppg_features_t latest_features;
static calibration_state_t calibration;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void nvs_load_calibration(void)
{
    calibration.valid = false;
    calibration.systolic = CONFIG_BP_DEFAULT_CAL_SBP;
    calibration.diastolic = CONFIG_BP_DEFAULT_CAL_DBP;
    calibration.baseline_bpm = 70.0f;
    calibration.baseline_pi = 0.01f;
    calibration.calibrated_at_ms = 0;

    nvs_handle_t handle;
    if (nvs_open("bp_cal", NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "no saved calibration, using defaults");
        return;
    }

    int32_t sbp = 0;
    int32_t dbp = 0;
    int32_t bpm_x10 = 0;
    int32_t pi_x100000 = 0;
    uint32_t calibrated_at = 0;

    esp_err_t ok = nvs_get_i32(handle, "sbp", &sbp);
    ok |= nvs_get_i32(handle, "dbp", &dbp);
    ok |= nvs_get_i32(handle, "bpm_x10", &bpm_x10);
    ok |= nvs_get_i32(handle, "pi_x100000", &pi_x100000);
    ok |= nvs_get_u32(handle, "time_ms", &calibrated_at);
    nvs_close(handle);

    if (ok == ESP_OK) {
        calibration.valid = true;
        calibration.systolic = (int)sbp;
        calibration.diastolic = (int)dbp;
        calibration.baseline_bpm = (float)bpm_x10 / 10.0f;
        calibration.baseline_pi = (float)pi_x100000 / 100000.0f;
        calibration.calibrated_at_ms = now_ms();
        ESP_LOGI(TAG, "loaded calibration %d/%d, bpm=%.1f, pi=%.5f",
                 calibration.systolic,
                 calibration.diastolic,
                 calibration.baseline_bpm,
                 calibration.baseline_pi);
    }
}

static esp_err_t nvs_save_calibration(const calibration_state_t *state)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open("bp_cal", NVS_READWRITE, &handle), TAG, "nvs open failed");

    esp_err_t err = ESP_OK;
    err |= nvs_set_i32(handle, "sbp", state->systolic);
    err |= nvs_set_i32(handle, "dbp", state->diastolic);
    err |= nvs_set_i32(handle, "bpm_x10", (int32_t)lroundf(state->baseline_bpm * 10.0f));
    err |= nvs_set_i32(handle, "pi_x100000", (int32_t)lroundf(state->baseline_pi * 100000.0f));
    err |= nvs_set_u32(handle, "time_ms", state->calibrated_at_ms);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void estimate_bp(const ppg_features_t *features, telemetry_reading_t *reading)
{
    calibration_state_t snapshot;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    snapshot = calibration;
    xSemaphoreGive(state_mutex);

    reading->calibrated = snapshot.valid;
    reading->calibration_age_sec = snapshot.calibrated_at_ms > 0
                                       ? (now_ms() - snapshot.calibrated_at_ms) / 1000U
                                       : 0U;

    if (!features->finger_detected || features->avg_bpm <= 0) {
        reading->systolic = 0;
        reading->diastolic = 0;
        return;
    }

    const float bpm_delta = (float)features->avg_bpm - snapshot.baseline_bpm;
    const float pi_delta = features->perfusion_index - snapshot.baseline_pi;

    // First-pass trend model. Replace this with the TinyML regressor once trained.
    const float systolic = (float)snapshot.systolic + 0.45f * bpm_delta - 800.0f * pi_delta;
    const float diastolic = (float)snapshot.diastolic + 0.25f * bpm_delta - 420.0f * pi_delta;

    reading->systolic = clamp_int((int)lroundf(systolic), 70, 220);
    reading->diastolic = clamp_int((int)lroundf(diastolic), 40, 140);
}

static void sensor_task(void *arg)
{
    (void)arg;

    max30102_t sensor = {0};
    esp_err_t err = max30102_init(&sensor, CONFIG_BP_I2C_SDA_GPIO, CONFIG_BP_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }

    while (true) {
        ppg_sample_t sample = {
            .timestamp_ms = now_ms(),
        };

        err = max30102_read_sample(&sensor, &sample.red, &sample.ir);
        if (err == ESP_OK) {
            if (xQueueSend(sample_queue, &sample, 0) != pdPASS) {
                ppg_sample_t dropped;
                (void)xQueueReceive(sample_queue, &dropped, 0);
                (void)xQueueSend(sample_queue, &sample, 0);
            }
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "sample read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static void processing_task(void *arg)
{
    (void)arg;

    ppg_processor_t processor;
    ppg_processor_init(&processor);

    uint32_t last_post_ms = 0;

    while (true) {
        ppg_sample_t sample;
        if (xQueueReceive(sample_queue, &sample, portMAX_DELAY) != pdPASS) {
            continue;
        }

        ppg_features_t features;
        ppg_processor_update(&processor, &sample, &features);

        xSemaphoreTake(state_mutex, portMAX_DELAY);
        latest_features = features;
        xSemaphoreGive(state_mutex);

        if (features.timestamp_ms - last_post_ms < CONFIG_BP_POST_INTERVAL_MS) {
            continue;
        }
        last_post_ms = features.timestamp_ms;

        telemetry_reading_t reading = {
            .timestamp_ms = features.timestamp_ms,
            .bpm = (int)lroundf(features.bpm),
            .avg_bpm = features.avg_bpm,
            .ir = features.ir_raw,
            .red = features.red_raw,
            .filtered_ir = features.filtered_ir,
            .perfusion_index = features.perfusion_index,
            .finger_detected = features.finger_detected,
        };

        estimate_bp(&features, &reading);

        if (xQueueSend(telemetry_queue, &reading, 0) != pdPASS) {
            telemetry_reading_t dropped;
            (void)xQueueReceive(telemetry_queue, &dropped, 0);
            (void)xQueueSend(telemetry_queue, &reading, 0);
        }
    }
}

static void tx_task(void *arg)
{
    (void)arg;

    esp_err_t err = wifi_http_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi start failed; telemetry task will keep retrying posts");
    }

    while (true) {
        telemetry_reading_t reading;
        if (xQueueReceive(telemetry_queue, &reading, portMAX_DELAY) == pdPASS) {
            (void)wifi_http_post_reading(&reading);
        }
    }
}

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  cal <systolic> <diastolic>  Save cuff calibration using the current PPG baseline\n");
    printf("  show                        Print current features and calibration\n");
    printf("  help                        Print this help\n\n");
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96];

    print_help();

    while (true) {
        printf("bp> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int sbp = 0;
        int dbp = 0;
        if (sscanf(line, "cal %d %d", &sbp, &dbp) == 2) {
            ppg_features_t features;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            features = latest_features;
            xSemaphoreGive(state_mutex);

            if (!features.finger_detected || features.avg_bpm <= 0) {
                printf("Calibration rejected: place finger and wait for a stable BPM first.\n");
                continue;
            }

            calibration_state_t next = {
                .valid = true,
                .systolic = clamp_int(sbp, 70, 220),
                .diastolic = clamp_int(dbp, 40, 140),
                .baseline_bpm = (float)features.avg_bpm,
                .baseline_pi = features.perfusion_index,
                .calibrated_at_ms = now_ms(),
            };

            esp_err_t err = nvs_save_calibration(&next);
            if (err == ESP_OK) {
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                calibration = next;
                xSemaphoreGive(state_mutex);
                printf("Saved calibration %d/%d at BPM=%d PI=%.5f\n",
                       next.systolic,
                       next.diastolic,
                       features.avg_bpm,
                       next.baseline_pi);
            } else {
                printf("Calibration save failed: %s\n", esp_err_to_name(err));
            }
        } else if (strncmp(line, "show", 4) == 0) {
            ppg_features_t features;
            calibration_state_t cal;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            features = latest_features;
            cal = calibration;
            xSemaphoreGive(state_mutex);

            printf("Finger=%s IR=%lu filtered=%.2f BPM=%d PI=%.5f\n",
                   features.finger_detected ? "yes" : "no",
                   (unsigned long)features.ir_raw,
                   features.filtered_ir,
                   features.avg_bpm,
                   features.perfusion_index);
            printf("Calibration=%s %d/%d baseline BPM=%.1f PI=%.5f age=%lu sec\n",
                   cal.valid ? "saved" : "default",
                   cal.systolic,
                   cal.diastolic,
                   cal.baseline_bpm,
                   cal.baseline_pi,
                   cal.calibrated_at_ms > 0 ? (unsigned long)((now_ms() - cal.calibrated_at_ms) / 1000U) : 0UL);
        } else {
            print_help();
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    sample_queue = xQueueCreate(CONFIG_BP_SAMPLE_QUEUE_LEN, sizeof(ppg_sample_t));
    telemetry_queue = xQueueCreate(4, sizeof(telemetry_reading_t));
    state_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(sample_queue != NULL && telemetry_queue != NULL && state_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    nvs_load_calibration();

    ESP_LOGI(TAG, "starting ESP32-S3 BP monitor foundation");
    ESP_LOGI(TAG, "I2C SDA=%d SCL=%d server=%s", CONFIG_BP_I2C_SDA_GPIO, CONFIG_BP_I2C_SCL_GPIO, CONFIG_BP_SERVER_URL);

    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 7, NULL, 0);
    xTaskCreatePinnedToCore(processing_task, "processing_task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(tx_task, "tx_task", 6144, NULL, 4, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(console_task, "console_task", 4096, NULL, 3, NULL, tskNO_AFFINITY);
}
