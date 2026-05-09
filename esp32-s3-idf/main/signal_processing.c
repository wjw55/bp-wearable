#include "signal_processing.h"

#include <math.h>
#include <string.h>

#define FINGER_IR_THRESHOLD 20000.0f
#define DC_ALPHA 0.01f
#define FILTER_ALPHA 0.18f
#define ENVELOPE_ALPHA 0.04f
#define MIN_BEAT_INTERVAL_MS 300
#define MAX_BEAT_INTERVAL_MS 2000

static float average_bpm(const ppg_processor_t *processor)
{
    if (processor->bpm_count == 0) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < processor->bpm_count; ++i) {
        sum += processor->bpm_history[i];
    }

    return sum / (float)processor->bpm_count;
}

static void clear_beats(ppg_processor_t *processor)
{
    processor->last_beat_ms = 0;
    processor->bpm_count = 0;
    processor->bpm_pos = 0;
    memset(processor->bpm_history, 0, sizeof(processor->bpm_history));
}

void ppg_processor_init(ppg_processor_t *processor)
{
    memset(processor, 0, sizeof(*processor));
}

void ppg_processor_update(ppg_processor_t *processor, const ppg_sample_t *sample, ppg_features_t *out)
{
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = sample->timestamp_ms;
    out->red_raw = sample->red;
    out->ir_raw = sample->ir;

    const float ir = (float)sample->ir;
    out->finger_detected = ir >= FINGER_IR_THRESHOLD;

    if (processor->dc_ir <= 1.0f) {
        processor->dc_ir = ir;
    } else {
        processor->dc_ir += DC_ALPHA * (ir - processor->dc_ir);
    }

    const float highpass = ir - processor->dc_ir;
    processor->filtered_ir += FILTER_ALPHA * (highpass - processor->filtered_ir);
    processor->envelope += ENVELOPE_ALPHA * (fabsf(processor->filtered_ir) - processor->envelope);

    out->filtered_ir = processor->filtered_ir;
    out->dc_ir = processor->dc_ir;
    out->perfusion_index = processor->dc_ir > 1.0f ? processor->envelope / processor->dc_ir : 0.0f;

    if (!out->finger_detected) {
        clear_beats(processor);
        processor->prev_derivative = 0.0f;
        processor->prev_filtered_ir = processor->filtered_ir;
        out->bpm = 0.0f;
        out->avg_bpm = 0;
        return;
    }

    const float derivative = processor->filtered_ir - processor->prev_filtered_ir;
    const float threshold = fmaxf(50.0f, processor->envelope * 0.60f);
    const bool local_peak = processor->prev_derivative > 0.0f &&
                            derivative <= 0.0f &&
                            processor->prev_filtered_ir > threshold;

    if (local_peak) {
        const int64_t now_ms = sample->timestamp_ms;
        if (processor->last_beat_ms > 0) {
            const int64_t interval_ms = now_ms - processor->last_beat_ms;
            if (interval_ms >= MIN_BEAT_INTERVAL_MS && interval_ms <= MAX_BEAT_INTERVAL_MS) {
                const float bpm = 60000.0f / (float)interval_ms;
                if (bpm >= 30.0f && bpm <= 220.0f) {
                    processor->bpm_history[processor->bpm_pos] = bpm;
                    processor->bpm_pos = (processor->bpm_pos + 1) % (sizeof(processor->bpm_history) / sizeof(processor->bpm_history[0]));
                    if (processor->bpm_count < (sizeof(processor->bpm_history) / sizeof(processor->bpm_history[0]))) {
                        processor->bpm_count++;
                    }
                    out->bpm = bpm;
                    out->beat_detected = true;
                }
            }
        }
        processor->last_beat_ms = now_ms;
    }

    const float avg = average_bpm(processor);
    out->avg_bpm = avg > 0.0f ? (int)lroundf(avg) : 0;
    if (out->bpm <= 0.0f && processor->bpm_count > 0) {
        out->bpm = processor->bpm_history[(processor->bpm_pos + 7) % 8];
    }

    processor->prev_derivative = derivative;
    processor->prev_filtered_ir = processor->filtered_ir;
}
