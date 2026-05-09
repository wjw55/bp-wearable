#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t timestamp_ms;
    int bpm;
    int avg_bpm;
    uint32_t ir;
    uint32_t red;
    float filtered_ir;
    float perfusion_index;
    int systolic;
    int diastolic;
    bool finger_detected;
    bool calibrated;
    uint32_t calibration_age_sec;
} telemetry_reading_t;
