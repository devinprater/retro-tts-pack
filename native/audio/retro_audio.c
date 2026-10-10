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
#include <string.h>

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

/* Python's // rounds towards minus infinity; C's / truncates towards zero. */
static long floor_div(long value, long divisor)
{
    long quotient = value / divisor;

    if ((value % divisor != 0) && ((value < 0) != (divisor < 0)))
        quotient--;
    return quotient;
}

/*
 * Change speaking rate by overlap-add, the way the pack's Python did it.
 *
 * L&H TTS3000's private manager ignores SAPI rate settings on its file-render
 * path, so the pack aligns overlapping waveform windows instead, which changes
 * duration while leaving the voices' pitch mostly alone.  Written out sample by
 * sample in Python it cost about half a second for a paragraph: 2937060
 * generator iterations, one per window offset per candidate.  The arithmetic is
 * the same here, and it is the same arithmetic: floor division, clamping, and
 * Python's round() for the hop, which is round-half-to-even.
 *
 * Writes at most out_capacity samples and returns how many, or -1 if the
 * arguments are unusable or the result would not fit.
 */
long retro_time_scale(const short *in, long in_samples, short *out,
                      long out_capacity, int sample_rate, double factor)
{
    long frame, overlap, output_hop, input_hop, search;
    long position, out_len, index;

    if (in == NULL || out == NULL || in_samples < 0 || out_capacity < 0)
        return -1;
    if (sample_rate <= 0)
        return -1;

    if (fabs(factor - 1.0) < 0.015) {
        if (in_samples > out_capacity)
            return -1;
        memcpy(out, in, (size_t)in_samples * sizeof *out);
        return in_samples;
    }

    frame = (long)sample_rate * 30 / 1000;
    if (frame < 96)
        frame = 96;
    overlap = (long)sample_rate * 10 / 1000;
    if (overlap < 32)
        overlap = 32;
    output_hop = frame - overlap;
    input_hop = (long)rint((double)output_hop * factor);
    if (input_hop < 1)
        input_hop = 1;
    search = (long)sample_rate * 4 / 1000;
    if (search < 8)
        search = 8;

    if (in_samples <= frame) {
        if (in_samples > out_capacity)
            return -1;
        memcpy(out, in, (size_t)in_samples * sizeof *out);
        return in_samples;
    }

    memcpy(out, in, (size_t)frame * sizeof *out);
    out_len = frame;
    position = input_hop;

    while (position + frame < in_samples) {
        long low = position - search < 0 ? 0 : position - search;
        long high = position + search;
        long best = position;
        long long best_score = 0;
        int have_score = 0;
        long candidate;
        long base = out_len - overlap;

        if (high > in_samples - frame)
            high = in_samples - frame;
        for (candidate = low; candidate <= high; candidate += 2) {
            long long score = 0;
            for (index = 0; index < overlap; index++)
                score += (long long)out[base + index] * in[candidate + index];
            if (!have_score || score > best_score) {
                best_score = score;
                best = candidate;
                have_score = 1;
            }
        }
        for (index = 0; index < overlap; index++) {
            long mixed = floor_div((long)out[base + index] * (overlap - index) +
                                       (long)in[best + index] * index,
                                   overlap);
            if (mixed > 32767)
                mixed = 32767;
            else if (mixed < -32768)
                mixed = -32768;
            out[base + index] = (short)mixed;
        }
        if (out_len + output_hop > out_capacity)
            return -1;
        memcpy(out + out_len, in + best + overlap,
               (size_t)output_hop * sizeof *out);
        out_len += output_hop;
        position = best + input_hop;
    }
    return out_len;
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
