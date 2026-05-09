#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t timestamp_ms;
    uint32_t red;
    uint32_t ir;
} ppg_sample_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t red_raw;
    uint32_t ir_raw;
    float filtered_ir;
    float dc_ir;
    float perfusion_index;
    float bpm;
    int avg_bpm;
    bool beat_detected;
    bool finger_detected;
} ppg_features_t;

typedef struct {
    float dc_ir;
    float filtered_ir;
    float prev_filtered_ir;
    float prev_derivative;
    float envelope;
    int64_t last_beat_ms;
    float bpm_history[8];
    size_t bpm_count;
    size_t bpm_pos;
} ppg_processor_t;

void ppg_processor_init(ppg_processor_t *processor);
void ppg_processor_update(ppg_processor_t *processor, const ppg_sample_t *sample, ppg_features_t *out);
