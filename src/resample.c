#include "resample.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Keep a short rolling window of the input. Only the frame at floor(pos) and
 * the one after it are needed for the next output, but retaining the final
 * frame also lets flush() extend the signal cleanly.
 */
static int ensure_history(Resampler *rs, size_t extra)
{
    size_t needed;
    size_t new_cap;
    size_t bytes;
    float *grown;

    if (extra > SIZE_MAX - rs->hist_len) {
        return -1;
    }
    needed = rs->hist_len + extra;
    if (needed <= rs->hist_cap) {
        return 0;
    }

    new_cap = rs->hist_cap ? rs->hist_cap : 256u;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2u) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    if ((size_t)rs->channels != 0u &&
        new_cap > SIZE_MAX / (size_t)rs->channels / sizeof(float)) {
        return -1;
    }
    bytes = new_cap * (size_t)rs->channels * sizeof(float);
    grown = (float *)realloc(rs->hist, bytes);
    if (grown == NULL) {
        return -1;
    }
    rs->hist = grown;
    rs->hist_cap = new_cap;
    return 0;
}

static float clamp_sample(float value)
{
    if (value > 1.0f) {
        return 1.0f;
    }
    if (value < -1.0f) {
        return -1.0f;
    }
    return value;
}

static const float *frame_at(const Resampler *rs, long long index)
{
    long long offset;

    if (index < rs->hist_start) {
        return NULL;
    }
    offset = index - rs->hist_start;
    if (offset < 0 || (unsigned long long)offset >= (unsigned long long)rs->hist_len) {
        return NULL;
    }
    return rs->hist + (size_t)offset * (size_t)rs->channels;
}

static long long target_output_frames(const Resampler *rs)
{
    double target;

    if (rs->total_in <= 0) {
        return 0;
    }
    target = (double)rs->total_in * rs->out_rate / rs->in_rate;
    /* The tiny subtraction absorbs harmless binary rounding around integers. */
    target = ceil(target - 1e-9);
    if (target < 0.0) {
        return 0;
    }
    return (long long)target;
}

static void compact_history(Resampler *rs)
{
    long long keep;
    size_t drop;
    size_t remaining;

    if (rs->hist_len == 0) {
        return;
    }

    keep = (long long)floor(rs->pos);
    if (keep > rs->total_in - 1) {
        keep = rs->total_in - 1;
    }
    if (keep < rs->hist_start) {
        keep = rs->hist_start;
    }

    drop = (size_t)(keep - rs->hist_start);
    if (drop == 0 || drop >= rs->hist_len) {
        return;
    }
    remaining = rs->hist_len - drop;
    memmove(rs->hist,
            rs->hist + drop * (size_t)rs->channels,
            remaining * (size_t)rs->channels * sizeof(float));
    rs->hist_len = remaining;
    rs->hist_start = keep;
}

int resample_init(Resampler *rs, double in_rate, double out_rate, int channels)
{
    if (rs == NULL) {
        return -1;
    }
    memset(rs, 0, sizeof(*rs));
    if (!isfinite(in_rate) || !isfinite(out_rate) || in_rate <= 0.0 ||
        out_rate <= 0.0 || channels <= 0) {
        return -1;
    }

    rs->in_rate = in_rate;
    rs->out_rate = out_rate;
    rs->step = in_rate / out_rate;
    if (!isfinite(rs->step) || rs->step <= 0.0) {
        return -1;
    }
    rs->channels = channels;
    rs->pos = 0.0;
    rs->initialized = 1;
    return 0;
}

void resample_free(Resampler *rs)
{
    if (rs == NULL) {
        return;
    }
    free(rs->hist);
    memset(rs, 0, sizeof(*rs));
}

int resample_process(Resampler *rs, const float *in, int in_frames,
                     float *out, int out_cap, int *out_frames)
{
    long long target;
    size_t produced = 0;

    if (rs == NULL || !rs->initialized || out_frames == NULL) {
        return -1;
    }
    *out_frames = 0;
    if (in_frames < 0 || out_cap < 0) {
        return -1;
    }
    if (in_frames > 0 && in == NULL) {
        return -1;
    }
    if (out_cap > 0 && out == NULL) {
        return -1;
    }

    if (in_frames > 0) {
        size_t frames = (size_t)in_frames;
        if (ensure_history(rs, frames) != 0) {
            return -1;
        }
        memcpy(rs->hist + rs->hist_len * (size_t)rs->channels,
               in,
               frames * (size_t)rs->channels * sizeof(float));
        rs->hist_len += frames;
        rs->total_in += in_frames;
    }

    target = target_output_frames(rs);
    while (produced < (size_t)out_cap && rs->total_out < target) {
        long long index = (long long)floor(rs->pos);
        double frac = rs->pos - (double)index;
        const float *a;
        const float *b;
        int ch;

        if (index < 0 || index + 1 >= rs->total_in) {
            break;
        }
        a = frame_at(rs, index);
        b = frame_at(rs, index + 1);
        if (a == NULL || b == NULL) {
            break;
        }

        for (ch = 0; ch < rs->channels; ++ch) {
            float value = a[ch] + (float)frac * (b[ch] - a[ch]);
            out[produced * (size_t)rs->channels + (size_t)ch] =
                clamp_sample(value);
        }
        rs->pos += rs->step;
        rs->total_out++;
        produced++;
    }

    compact_history(rs);
    *out_frames = (int)produced;
    return 0;
}

int resample_flush(Resampler *rs, float *out, int out_cap, int *out_frames)
{
    long long target;
    long long last;
    size_t produced = 0;

    if (rs == NULL || !rs->initialized || out_frames == NULL) {
        return -1;
    }
    *out_frames = 0;
    if (out_cap < 0) {
        return -1;
    }
    if (out_cap > 0 && out == NULL) {
        return -1;
    }
    if (rs->total_in <= 0) {
        return 0;
    }

    target = target_output_frames(rs);
    last = rs->total_in - 1;
    while (produced < (size_t)out_cap && rs->total_out < target) {
        long long index = (long long)floor(rs->pos);
        double frac = rs->pos - (double)index;
        long long ia = index < last ? index : last;
        long long ib = index + 1 < last ? index + 1 : last;
        const float *a = frame_at(rs, ia);
        const float *b = frame_at(rs, ib);
        int ch;

        if (a == NULL || b == NULL) {
            break;
        }
        /* Past the final input frame the tail extends the last sample. */
        for (ch = 0; ch < rs->channels; ++ch) {
            float value = a[ch] + (float)frac * (b[ch] - a[ch]);
            out[produced * (size_t)rs->channels + (size_t)ch] =
                clamp_sample(value);
        }
        rs->pos += rs->step;
        rs->total_out++;
        produced++;
    }

    *out_frames = (int)produced;
    return 0;
}
