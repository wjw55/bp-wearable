#include "wifi_http.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_MAX_RETRY 8

static const char *TAG = "wifi_http";
static EventGroupHandle_t wifi_event_group;
static int retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (retry_count < WIFI_MAX_RETRY) {
            retry_count++;
            ESP_LOGW(TAG, "wifi disconnected, retry %d/%d", retry_count, WIFI_MAX_RETRY);
        } else {
            retry_count = 1;
            ESP_LOGW(TAG, "wifi still disconnected, continuing retries");
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "wifi connected");
    }
}

esp_err_t wifi_http_start(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", CONFIG_BP_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", CONFIG_BP_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_http_post_reading(const telemetry_reading_t *reading)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[384];
    int written = snprintf(
        payload,
        sizeof(payload),
        "{\"bpm\":%d,\"avgBpm\":%d,\"ir\":%lu,\"red\":%lu,"
        "\"filteredIr\":%.2f,\"perfusionIndex\":%.6f,"
        "\"systolic\":%d,\"diastolic\":%d,"
        "\"fingerDetected\":%s,\"calibrated\":%s,"
        "\"calibrationAgeSec\":%lu,\"timestamp\":%lu}",
        reading->bpm,
        reading->avg_bpm,
        (unsigned long)reading->ir,
        (unsigned long)reading->red,
        reading->filtered_ir,
        reading->perfusion_index,
        reading->systolic,
        reading->diastolic,
        reading->finger_detected ? "true" : "false",
        reading->calibrated ? "true" : "false",
        (unsigned long)reading->calibration_age_sec,
        (unsigned long)reading->timestamp_ms);

    if (written < 0 || written >= (int)sizeof(payload)) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_BP_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 1000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "posted telemetry, status=%d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "post failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
