#ifndef MINI_AUDIO_RESAMPLE_H
#define MINI_AUDIO_RESAMPLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Streaming, phase-accumulating linear resampler.
 *
 * The public struct is intentionally visible so callers can allocate a
 * Resampler on the stack. Treat every field as private implementation state.
 */
typedef struct Resampler {
    double in_rate;
    double out_rate;
    double step;       /* input frames advanced per output frame */
    double pos;        /* absolute position of the next output frame */
    long long total_in;
    long long total_out;
    int channels;
    int initialized;

    float *hist;
    size_t hist_len;
    size_t hist_cap;
    long long hist_start;
} Resampler;

/*
 * Prepare a resampler. Returns 0 on success and -1 on invalid arguments or
 * allocation failure. Input and output rates are in Hz; channels is the
 * interleaved channel count.
 */
int resample_init(Resampler *rs, double in_rate, double out_rate, int channels);

/*
 * Resample up to in_frames interleaved input frames. At most out_cap output
 * frames are written. The number actually written is returned via out_frames.
 * A frame whose interpolation would need a not-yet-available sample is held
 * back for the next call or for resample_flush().
 *
 * Passing in == NULL with in_frames == 0 is allowed and can drain buffered
 * output when out_cap was too small on a previous call.
 */
int resample_process(Resampler *rs, const float *in, int in_frames,
                     float *out, int out_cap, int *out_frames);

/*
 * Emit the remaining output frames after the final input frame. The tail is
 * extended with the final input sample so the total output frame count is
 * ceil(total_in_frames * out_rate / in_rate). Returns 0 on success.
 */
int resample_flush(Resampler *rs, float *out, int out_cap, int *out_frames);

/* Release all memory owned by the resampler. Safe to call with NULL. */
void resample_free(Resampler *rs);

#ifdef __cplusplus
}
#endif

#endif /* MINI_AUDIO_RESAMPLE_H */
