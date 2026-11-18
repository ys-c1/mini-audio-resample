# mini-audio-resample

A tiny C11 streaming audio resampler with no third-party dependencies. It uses
a phase accumulator plus linear interpolation, supports arbitrary input/output
ratios, and handles interleaved mono or multichannel audio.

## Build and test

```sh
make test
```

The Makefile builds with `gcc -std=c11 -O2 -lm` by default. Build products go
into `build/`.

## API

```c
int resample_init(Resampler *rs, double in_rate, double out_rate, int channels);
int resample_process(Resampler *rs, const float *in, int in_frames,
                     float *out, int out_cap, int *out_frames);
int resample_flush(Resampler *rs, float *out, int out_cap, int *out_frames);
void resample_free(Resampler *rs);
```

`resample_process()` consumes interleaved frames and writes as many complete
output frames as the available input permits. `resample_flush()` emits the
remaining tail, extending the last input sample, so the total output frame
count is `ceil(in_frames * out_rate / in_rate)`.

Normalized samples are clamped to `[-1, 1]` on output.

## Test coverage

`tests/test_resample.c` checks:

- 48 kHz to 48 kHz passthrough, sample for sample
- 48 kHz to 24 kHz downsampling, frame count and sine frequency
- 24 kHz to 48 kHz upsampling, frame count and output bounds
- flush completion for a 44.1 kHz to 48 kHz conversion
- stereo channel separation
- output clamping for over-full-scale material

## Repository state

The committed source snapshot is the intentionally buggy variant used for a
debugging exercise. Three defects are seeded in `src/resample.c`; the test
suite is expected to report assertion failures. The tests themselves are not
modified.
