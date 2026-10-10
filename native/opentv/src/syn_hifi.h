/*
 * The resonator tables for OpenTV's extra sample rate, shared by every engine.
 *
 * TruVoice shipped tables for 8 kHz and 11.025 kHz and nothing else, because
 * Centigram never offered a third rate.  These are computed instead of lifted:
 * the formulas in tools/gen_synth_hifi.py reproduce both of the original's sets
 * exactly -- all 2,120 values, in the 1997 English engine and in the 1995
 * Spanish one alike, which are byte-identical here -- so evaluating them at a
 * third rate is reading the same design rather than inventing one.
 *
 * That the two engines' tables agree is why there is one copy of these: the
 * rate-dependent part of the synthesiser did not change between the
 * generations, so neither does its extension.  Each engine's Synth_InitFilters
 * points at them when it is asked for this rate.
 *
 * The tables are OpenTV's own work and not Centigram's.  See NOTICE.
 */
#ifndef TV_SYN_HIFI_H
#define TV_SYN_HIFI_H

#include <stdint.h>

/* The rate the tables were generated for.  Each engine checks its own name for
 * this rate against it, so the two cannot drift apart. */
#define TV_SYNHIFI_RATE 16000

extern const int32_t g_synhifi_6[180];
extern const int32_t g_synhifi_7[180];
/* Frequency lookups step by 8 Hz, including the Nyquist endpoint. */
#define TV_SYNHIFI_FREQ_COUNT (TV_SYNHIFI_RATE / 16 + 1)
extern const int32_t g_synhifi_8[TV_SYNHIFI_FREQ_COUNT];
/* The fixed resonator a frame pairs with a frequency from track 16. */
extern const int32_t g_synhifi_2038;
/* filt_coef[12], [13] and [33]: the same resonator, and the only entries of the
 * initial coefficient array that depend on the rate and are never rewritten
 * per frame. */
extern const int32_t g_synhifi_c12;
extern const int32_t g_synhifi_c13;

#endif
