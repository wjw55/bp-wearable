#pragma once

#include "esp_err.h"
#include "telemetry.h"

esp_err_t wifi_http_start(void);
esp_err_t wifi_http_post_reading(const telemetry_reading_t *reading);
