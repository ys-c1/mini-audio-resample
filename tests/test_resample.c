#include "resample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

#define MAX_IN_FRAMES 48000
#define MAX_STEREO_SAMPLES (MAX_IN_FRAMES * 2)
#define MAX_OUT_SAMPLES 100000

static float g_in[MAX_STEREO_SAMPLES];
static float g_out[MAX_OUT_SAMPLES];

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void fill_sine_mono(float *buf, int frames, double fs, double freq,
                           double amp)
{
    int i;

    for (i = 0; i < frames; ++i) {
        buf[i] = (float)(amp * sin(2.0 * PI * freq * (double)i / fs));
    }
}

static void fill_sine_stereo(float *buf, int frames, double fs, double left_freq,
                             double right_freq, double amp)
{
    int i;

    for (i = 0; i < frames; ++i) {
        double t = (double)i / fs;
        buf[2 * i] = (float)(amp * sin(2.0 * PI * left_freq * t));
        buf[2 * i + 1] =
            (float)(0.8 * amp * sin(2.0 * PI * right_freq * t + 0.7));
    }
}

static double max_abs_diff_mono(const float *a, const float *b, int frames)
{
    double worst = 0.0;
    int i;

    for (i = 0; i < frames; ++i) {
        double diff = fabs((double)a[i] - (double)b[i]);
        if (diff > worst) {
            worst = diff;
        }
    }
    return worst;
}

static double max_abs_diff_stereo_channel(const float *a, const float *b,
                                          int frames, int channel)
{
    double worst = 0.0;
    int i;

    for (i = 0; i < frames; ++i) {
        double diff =
            fabs((double)a[2 * i + channel] - (double)b[2 * i + channel]);
        if (diff > worst) {
            worst = diff;
        }
    }
    return worst;
}

static double zero_crossing_frequency(const float *buf, int frames, double fs)
{
    int crossings = 0;
    int i;

    if (frames < 2 || fs <= 0.0) {
        return 0.0;
    }
    for (i = 1; i < frames; ++i) {
        float previous = buf[i - 1];
        float current = buf[i];
        if ((previous < 0.0f && current >= 0.0f) ||
            (previous >= 0.0f && current < 0.0f)) {
            ++crossings;
        }
    }
    return (double)crossings / (2.0 * ((double)frames / fs));
}

static double peak_abs_mono(const float *buf, int frames)
{
    double peak = 0.0;
    int i;

    for (i = 0; i < frames; ++i) {
        double value = fabs((double)buf[i]);
        if (value > peak) {
            peak = value;
        }
    }
    return peak;
}

static int expected_frames(int in_frames, double in_rate, double out_rate)
{
    double count = (double)in_frames * out_rate / in_rate;
    return (int)ceil(count - 1e-9);
}

static void test_passthrough_48k_to_48k(void)
{
    enum { N = 4800 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    double error;

    printf("[test] 48k -> 48k passthrough\n");
    memset(g_out, 0, sizeof(g_out));
    fill_sine_mono(g_in, N, 48000.0, 1000.0, 0.75);

    CHECK(resample_init(&rs, 48000.0, 48000.0, 1) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, N, &produced) == 0);
    CHECK(produced == N - 1);
    CHECK(resample_flush(&rs, g_out + produced, N - produced, &tail) == 0);
    CHECK(tail == 1);
    CHECK(produced + tail == N);
    error = max_abs_diff_mono(g_out, g_in, N);
    CHECK(error < 1e-6);
    resample_free(&rs);
}

static void test_downsample_48k_to_24k(void)
{
    enum { N = 48000 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    int total;
    double frequency;
    double peak;

    printf("[test] 48k -> 24k downsample\n");
    fill_sine_mono(g_in, N, 48000.0, 1000.0, 0.8);

    CHECK(resample_init(&rs, 48000.0, 24000.0, 1) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, N, &produced) == 0);
    CHECK(produced >= N / 2 - 2 && produced <= N / 2 + 2);
    CHECK(resample_flush(&rs, g_out + produced, N - produced, &tail) == 0);
    CHECK(tail == 0);
    total = produced + tail;
    CHECK(abs(total - N / 2) <= 1);
    peak = peak_abs_mono(g_out, total);
    CHECK(peak <= 1.001);
    frequency = zero_crossing_frequency(g_out, total, 24000.0);
    CHECK(fabs(frequency - 1000.0) <= 25.0);
    resample_free(&rs);
}

static void test_upsample_24k_to_48k(void)
{
    enum { N = 24000, OUT_CAP = 48010 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    int total;
    int expected;
    double frequency;

    printf("[test] 24k -> 48k upsample\n");
    fill_sine_mono(g_in, N, 24000.0, 1000.0, 0.8);

    CHECK(resample_init(&rs, 24000.0, 48000.0, 1) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, OUT_CAP, &produced) == 0);
    CHECK(produced >= 2 * N - 5 && produced <= 2 * N);
    CHECK(resample_flush(&rs, g_out + produced, OUT_CAP - produced, &tail) == 0);
    CHECK(tail >= 1 && tail <= 4);
    total = produced + tail;
    expected = expected_frames(N, 24000.0, 48000.0);
    CHECK(abs(total - expected) <= 1);
    frequency = zero_crossing_frequency(g_out, total, 48000.0);
    CHECK(fabs(frequency - 1000.0) <= 25.0);
    resample_free(&rs);
}

static void test_flush_total_44100_to_48000(void)
{
    enum { N = 44100, OUT_CAP = 48016 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    int total;
    int expected;
    double frequency;

    printf("[test] 44.1k -> 48k flush total\n");
    fill_sine_mono(g_in, N, 44100.0, 1000.0, 0.7);

    CHECK(resample_init(&rs, 44100.0, 48000.0, 1) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, OUT_CAP, &produced) == 0);
    CHECK(resample_flush(&rs, g_out + produced, OUT_CAP - produced, &tail) == 0);
    CHECK(tail >= 1);
    total = produced + tail;
    expected = expected_frames(N, 44100.0, 48000.0);
    CHECK(expected == 48000);
    CHECK(abs(total - expected) <= 1);
    frequency = zero_crossing_frequency(g_out, total, 48000.0);
    CHECK(fabs(frequency - 1000.0) <= 25.0);
    resample_free(&rs);
}

static void test_stereo_channels_do_not_mix(void)
{
    enum { N = 4800 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    int total;
    double left_error;
    double right_error;

    printf("[test] stereo channel separation\n");
    memset(g_out, 0, sizeof(g_out));
    fill_sine_stereo(g_in, N, 48000.0, 1000.0, 440.0, 0.6);

    CHECK(resample_init(&rs, 48000.0, 48000.0, 2) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, N, &produced) == 0);
    CHECK(produced == N - 1);
    CHECK(resample_flush(&rs, g_out + 2 * produced, N - produced, &tail) == 0);
    CHECK(tail == 1);
    total = produced + tail;
    CHECK(total == N);
    left_error = max_abs_diff_stereo_channel(g_out, g_in, N, 0);
    right_error = max_abs_diff_stereo_channel(g_out, g_in, N, 1);
    CHECK(left_error < 1e-6);
    CHECK(right_error < 1e-6);
    resample_free(&rs);
}

static void test_clamp_full_scale(void)
{
    enum { N = 2400, OUT_CAP = 4810 };
    Resampler rs;
    int produced = 0;
    int tail = 0;
    int total;
    int expected;
    int i;
    int all_high_ok = 1;
    int all_low_ok = 1;
    double peak;

    printf("[test] full-scale clamping\n");
    for (i = 0; i < N; ++i) {
        double t = (double)i / 24000.0;
        g_in[i] = (float)(1.35 * sin(2.0 * PI * 1000.0 * t) +
                          0.35 * sin(2.0 * PI * 3000.0 * t));
    }
    memset(g_out, 0, sizeof(g_out));

    CHECK(resample_init(&rs, 24000.0, 48000.0, 1) == 0);
    CHECK(resample_process(&rs, g_in, N, g_out, OUT_CAP, &produced) == 0);
    CHECK(produced > 0);
    CHECK(resample_flush(&rs, g_out + produced, OUT_CAP - produced, &tail) == 0);
    total = produced + tail;
    expected = expected_frames(N, 24000.0, 48000.0);
    CHECK(abs(total - expected) <= 1);

    for (i = 0; i < total; ++i) {
        if (g_out[i] > 1.000001f) {
            all_high_ok = 0;
        }
        if (g_out[i] < -1.000001f) {
            all_low_ok = 0;
        }
    }
    peak = peak_abs_mono(g_out, total);
    CHECK(all_high_ok);
    CHECK(all_low_ok);
    CHECK(peak <= 1.000001);
    CHECK(peak >= 0.99);
    resample_free(&rs);
}

int main(void)
{
    test_passthrough_48k_to_48k();
    test_downsample_48k_to_24k();
    test_upsample_24k_to_48k();
    test_flush_total_44100_to_48000();
    test_stereo_channels_do_not_mix();
    test_clamp_full_scale();

    printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("TESTS FAILED\n");
    return 1;
}
