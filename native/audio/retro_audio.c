/*
 * retro_audio -- the pack's pause shortener, in C.
 *
 * The pack applies the NVDA add-ons' 30% short-pause policy to the output of
 * every engine it renders.  In Python that walked every 5 ms window of every
 * utterance a sample at a time, which measured 0.3 ms on a word, 1.6 ms on a
 * line and 10.9 ms on a paragraph -- more than the engine itself cost.  The
 * arithmetic is small and the same every time, so it lives here now.
 *
 * This mirrors engines/audio.py exactly, including which windows count as
 * quiet, how a run of quiet windows is shortened, and the two cases where only
 * the last window of a run survives.  It is checked against the Python by
 * rendering the same audio both ways and comparing the bytes.
 *
 * Samples are read little-endian, as the Python does on every machine this
 * pack supports.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define RETRO_QUIET 1
#define RETRO_LOUD 0

static unsigned long long square_sum(const unsigned char *block, long bytes,
                                     int sample_width)
{
    long i, count = bytes / sample_width;
    unsigned long long total = 0;

    if (sample_width == 1) {
        for (i = 0; i < count; i++) {
            long value = ((long)block[i] - 128) << 8;
            total += (unsigned long long)(value * value);
        }
    } else {
        for (i = 0; i < count; i++) {
            int value = (int)(int16_t)((unsigned)block[2 * i] |
                                       ((unsigned)block[2 * i + 1] << 8));
            total += (unsigned long long)((long)value * value);
        }
    }
    return total;
}

/* Exact integer square root, so the comparison matches Python's math.isqrt. */
static unsigned long long integer_sqrt(unsigned long long value)
{
    unsigned long long root = (unsigned long long)sqrtl((long double)value);

    while (root > 0 && root > value / root)
        root--;
    while ((root + 1) <= value / (root + 1))
        root++;
    return root;
}

/* Whether a window counts as quiet.  An empty one does, as in the Python. */
int retro_pcm_quiet(const unsigned char *block, long bytes, int sample_width,
                    int threshold)
{
    long count;

    if (block == NULL || bytes <= 0)
        return RETRO_QUIET;
    count = bytes / sample_width;
    if (count <= 0)
        return RETRO_QUIET;
    return integer_sqrt(square_sum(block, bytes, sample_width) / (unsigned long long)count)
                   < (unsigned long long)threshold
               ? RETRO_QUIET
               : RETRO_LOUD;
}

/* One quiet run: its first window, how many windows it holds, and how long the
   last of them is (only the final window of the audio may be short). */
typedef struct {
    long first;
    long count;
    long tail_bytes; /* length of the last window, or 0 when all are full */
} quiet_run;

static long window_offset(long index, long window_bytes)
{
    return index * window_bytes;
}

static long window_length(long index, long window_bytes, long total_bytes)
{
    long left = total_bytes - index * window_bytes;
    return left < window_bytes ? left : window_bytes;
}

static void emit(const unsigned char *in, unsigned char *out, long *written,
                 long offset, long bytes)
{
    long i;
    for (i = 0; i < bytes; i++)
        out[(*written) + i] = in[offset + i];
    *written += bytes;
}

static long run_keep(const quiet_run *run, int started, int trailing,
                     int minimum_windows, double factor)
{
    long keep;

    if (!started || trailing)
        return 1;
    if (run->count < minimum_windows)
        return run->count;
    keep = (long)rint((double)run->count * factor);
    if (keep < 1)
        keep = 1;
    return keep;
}

/*
 * Shorten the pauses in a whole buffer of mono PCM and return how many bytes
 * were written, or -1 if the arguments are unusable or the result would not
 * fit in out_capacity.
 */
long retro_shorten_pcm(const unsigned char *in, long in_bytes,
                       unsigned char *out, long out_capacity,
                       int sample_width, int sample_rate, int threshold,
                       int window_ms, int minimum_pause_ms, double factor)
{
    long window_samples, window_bytes, total_blocks, index, written = 0;
    int minimum_windows, started = 0;
    quiet_run run;

    if (in == NULL || out == NULL || in_bytes < 0 || out_capacity < 0)
        return -1;
    if (sample_width != 1 && sample_width != 2)
        return -1;
    if (sample_rate <= 0 || window_ms <= 0 || minimum_pause_ms <= 0)
        return -1;

    window_samples = (long)sample_rate * window_ms / 1000;
    if (window_samples < 1)
        window_samples = 1;
    window_bytes = window_samples * sample_width;
    minimum_windows = minimum_pause_ms / window_ms;
    if (minimum_windows < 1)
        minimum_windows = 1;

    total_blocks = in_bytes == 0 ? 0 : (in_bytes + window_bytes - 1) / window_bytes;
    run.first = -1;
    run.count = 0;
    run.tail_bytes = 0;

    for (index = 0; index < total_blocks; index++) {
        long offset = window_offset(index, window_bytes);
        long length = window_length(index, window_bytes, in_bytes);

        if (retro_pcm_quiet(in + offset, length, sample_width, threshold)) {
            if (run.count == 0)
                run.first = index;
            run.count++;
            run.tail_bytes = length;
            continue;
        }
        if (run.count > 0) {
            long keep = run_keep(&run, started, 0, minimum_windows, factor);
            long bytes;
            if (keep == 1 && (!started)) {
                emit(in, out, &written, window_offset(run.first + run.count - 1,
                                                      window_bytes),
                     run.tail_bytes);
            } else {
                long full = run.count - 1; /* windows before the last one */
                bytes = keep <= full ? keep * window_bytes
                                     : full * window_bytes + run.tail_bytes;
                emit(in, out, &written, window_offset(run.first, window_bytes), bytes);
            }
            run.first = -1;
            run.count = 0;
        }
        started = 1;
        emit(in, out, &written, offset, length);
        if (written > out_capacity)
            return -1;
    }

    if (run.count > 0) {
        long keep = run_keep(&run, started, 1, minimum_windows, factor);
        if (keep == 1) {
            emit(in, out, &written,
                 window_offset(run.first + run.count - 1, window_bytes),
                 run.tail_bytes);
        } else {
            long full = run.count - 1;
            long bytes = keep <= full ? keep * window_bytes
                                      : full * window_bytes + run.tail_bytes;
            emit(in, out, &written, window_offset(run.first, window_bytes), bytes);
        }
    }

    if (written > out_capacity)
        return -1;
    return written;
}
