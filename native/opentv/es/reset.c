/*
 * The reset chain.
 *
 * Engine_Reset calls eleven of these in a fixed order and Engine_Init calls
 * the same set; between them they put every subsystem back to its starting
 * state.  Six are written here.  They are almost all stores, which makes
 * them cheap to read and cheap to test -- poison the object, run one, and
 * compare it whole -- but they are also where the 1995 object's layout gets
 * pinned down, because a reset names fields by writing them.
 *
 * Output_Reset is the one that earned its keep.  It writes exactly the
 * fields the English Output_Reset writes, in the same order, each at
 * +0x19e4, and it does not write the one English ends with.  There is no
 * o_20e8 in this engine, which is why the host interface block that follows
 * sits at +0x19e8 rather than +0x19e4 -- a step that had been read off the
 * constructor's field order and now has a second, independent witness.
 *
 * Synth_ResetTracks carries the other find: after computing trk_08 from
 * trk_0c, exactly as English does, it stores -1 over it.  The first store is
 * dead.  It is kept here because the point of this file is what the original
 * does, and a reader comparing the two engines should be able to see that
 * the 1995 build has one store the 1997 build does not.
 */
#include "es_engine.h"

/* Initial raw value of each synthesis parameter track.  Byte for byte the
 * same 22 values as the English g_param_init at 0x100f8438. */
/* @0x100613b0 */
extern const uint8_t g_param_init[22];

/* @0x10007790 */
void TV_THISCALL Preformat_Reset(Engine *self)
{
    self->esc_state = 1;
    self->pre_busy = 0;
}

/* @0x1000f070 */
void TV_THISCALL Prosody_Reset(Engine *self)
{
    self->e_206c = 85;          /* the default pitch */
    self->e_2074 = 0;
    self->e_2070 = 0;
}

/* @0x1000fa10 */
void TV_THISCALL Stage1_Reset(Engine *self)
{
    self->stage_ctx[1].type_mask = 6;
    self->s1_8834 = 0;
}

/* The five seeded values are all different from English's, which writes 3,
 * -1, -1, 180 and -1 into the corresponding fields.  The 1995 stage 2 keeps
 * different state, so only the offsets carry over. */
/* @0x1001a8b0 */
void TV_THISCALL Stage2_Reset(Engine *self)
{
    self->stage_ctx[2].type_mask = 0x1c;
    self->s2_87f9 = 1;
    self->s2_87fc = 0xd;
    self->s2_87f4 = 5;
    self->s2_87b8 = 7;
    self->s2_87e0 = 0x14;
    self->s2_87d2 = 0;
    self->s2_87d4 = NULL;
    self->s2_87d8 = 0;
    self->s2_87e8 = 0;
    self->s2_8808 = 0;
    self->s2_87dc = 0;
    self->s2_87b4 = 0;
    self->s2_37c = 0;
    self->s2_378 = 0;
}

/* Every track restarts holding its initial value for 12 frames, as English's
 * does.  The trailing -1 into trk_08 is this engine's, and it makes the
 * store above it dead. */
/* @0x10017850 */
void TV_THISCALL Synth_ResetTracks(Engine *self)
{
    int i, j;

    for (i = 0; i < 22; i++) {
        self->trk_rd[i] = 10;
        self->trk_wr[i] = 12;
        self->trk_buf[i] = self->trk_data[i];
        for (j = 0; j < 12; j++)
            self->trk_buf[i][j] = g_param_init[i];
    }
    self->trk_04 = 0;
    self->trk_38 = 0;
    self->trk_10 = 12;
    self->trk_0c = 12;
    self->trk_34 = 0;
    for (i = 0; i < 32; i++) {
        self->trk_14[i] = 0;
        self->s3_1fbd[i] = 0;
    }
    self->trk_08 = self->trk_0c - 2;
    self->synth_busy = 0;
    self->trk_08 = -1;
}

/*
 * The synthesis tables and resonator constants for the output sample rate.
 *
 * The original builds both coefficient sets on the stack as int32 immediates
 * and narrows the chosen one into filt_coef with "mov word ptr [ecx], si";
 * they are written out as int16 tables here, which is the same thing and
 * readable.  English does exactly the same, with the two arrays in the
 * opposite stack halves.
 *
 * All twenty tables the two engines select between are byte-identical, and
 * so are the six resonator constants.  The 11 kHz coefficients are English's
 * to the bit.  The 8 kHz ones are not: this engine has -23934 and 17481
 * where the 1997 English build has -24759 and 20770, which are its own
 * 11 kHz values in those two slots.  Two coefficients out of forty, in the
 * rate the phone-quality output uses -- a real difference between the
 * generations rather than a transcription slip, since English's own tables
 * were re-read out of CGRM_EN.DLL to check it.
 */
/* OpenTV's third rate, shared with the English engine: see src/syn_hifi.h for
 * why one copy serves both. */
#include "syn_hifi.h"

/* The name this engine gives that rate has to be the rate the tables were made
 * for, or the filters would be built for one and the output stage run at the
 * other. */
typedef char tv_es_hifi_rate_agrees[(TV_SR_HIFI_ES == TV_SYNHIFI_RATE) ? 1 : -1];

/* @0x10049cc0 */ extern const uint8_t g_syn8k_9[];
/* @0x10049fd8 */ extern const uint8_t g_syn8k_6[];
/* @0x1004a578 */ extern const uint8_t g_syn8k_7[];
/* @0x1004b338 */ extern const uint8_t g_syn8k_8[];
/* @0x1004c9b8 */ extern const uint8_t g_syn8k_0[];
/* @0x1004c9e0 */ extern const uint8_t g_syn8k_1[];
/* @0x1004ca08 */ extern const uint8_t g_syn8k_2[];
/* @0x1004caa8 */ extern const uint8_t g_syn8k_3[];
/* @0x1004cad0 */ extern const uint8_t g_syn8k_4[];
/* @0x1004caf8 */ extern const uint8_t g_syn8k_5[];
/* @0x10049c78 */ extern const uint8_t g_syn11k_9[];
/* @0x10049d08 */ extern const uint8_t g_syn11k_6[];
/* @0x1004a2a8 */ extern const uint8_t g_syn11k_7[];
/* @0x1004a848 */ extern const uint8_t g_syn11k_8[];
/* @0x1004c940 */ extern const uint8_t g_syn11k_0[];
/* @0x1004c968 */ extern const uint8_t g_syn11k_1[];
/* @0x1004c990 */ extern const uint8_t g_syn11k_2[];
/* @0x1004ca30 */ extern const uint8_t g_syn11k_3[];
/* @0x1004ca58 */ extern const uint8_t g_syn11k_4[];
/* @0x1004ca80 */ extern const uint8_t g_syn11k_5[];

static const int16_t filt_coef_8k[40] = {
    0, 0, -23934, 17481, 0, 0, 0, 25889, 0, 0,
    0, 0, 30914, 30245, 0, 0, 0, 0, 0, 8192,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 30245, 0, 0, 0, 11354, 0, 0,
};

static const int16_t filt_coef_11k[40] = {
    0, 0, -24759, 20770, -24550, 28898, -19553, 27618, 0, 0,
    0, 0, 31521, 30905, 0, 0, 0, 17368, 0, 8192,
    0, 27691, 0, 25148, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 30905, 0, 0, 0, 11354, 0, 0,
};

/* @0x1000e6f0 */
void TV_THISCALL Synth_InitFilters(Engine *self)
{
    const int16_t *coef;
    int hifi = 0;
    int i;

    self->synth_19ad = 0;
    self->synth_19ae = 0;
    self->synth_hold = 0;
    if (self->sample_rate == 8000) {
        self->syn_tab[9] = g_syn8k_9;
        self->syn_tab[6] = g_syn8k_6;
        self->syn_tab[7] = g_syn8k_7;
        self->syn_tab[8] = g_syn8k_8;
        self->syn_tab[0] = g_syn8k_0;
        self->syn_tab[1] = g_syn8k_1;
        self->syn_tab[2] = g_syn8k_2;
        self->syn_tab[3] = g_syn8k_3;
        self->syn_tab[4] = g_syn8k_4;
        self->syn_tab[5] = g_syn8k_5;
        self->syn_2038 = -7870;
        self->syn_203c = 7281;
        self->syn_2040 = -6472;
        coef = filt_coef_8k;
    } else {
        self->syn_tab[9] = g_syn11k_9;
        self->syn_tab[6] = g_syn11k_6;
        self->syn_tab[7] = g_syn11k_7;
        self->syn_tab[8] = g_syn11k_8;
        self->syn_tab[0] = g_syn11k_0;
        self->syn_tab[1] = g_syn11k_1;
        self->syn_tab[2] = g_syn11k_2;
        self->syn_tab[3] = g_syn11k_3;
        self->syn_tab[4] = g_syn11k_4;
        self->syn_tab[5] = g_syn11k_5;
        self->syn_2038 = -7956;
        self->syn_203c = 7521;
        self->syn_2040 = -6905;
        coef = filt_coef_11k;
        /* OpenTV: the extra rate keeps the wideband tables above -- they are not
         * 11.025-specific, they are what the engine uses whenever it is not at
         * 8 kHz -- and swaps the three that really do depend on the rate.  The
         * same three English swaps, from the same tables. */
        if (self->sample_rate == TV_SR_HIFI_ES) {
            self->syn_tab[6] = g_synhifi_6;
            self->syn_tab[7] = g_synhifi_7;
            self->syn_tab[8] = g_synhifi_8;
            self->syn_2038 = g_synhifi_2038;
            hifi = 1;
        }
    }
    for (i = 0; i < 40; i++)
        self->filt_coef[i] = coef[i];
    /* Three entries of that array are a fixed 242 Hz resonator rather than a
     * starting value, and nothing rewrites them per frame, so at the extra rate
     * they have to be replaced too or the filter keeps 11 kHz damping and the
     * output rolls off far too steeply. */
    if (hifi) {
        self->filt_coef[12] = (int16_t)g_synhifi_c12;
        self->filt_coef[13] = (int16_t)g_synhifi_c13;
        self->filt_coef[33] = (int16_t)g_synhifi_c13;
    }
}

/* The sample rate arrives as the engine's own int16 field and is divided by
 * 100 in 16 bits, which is what o_rate_div100 holds for the rest of the run. */
/* @0x1000ad50 */
void TV_THISCALL Output_Reset(Engine *self, int16_t sample_rate)
{
    int i;

    self->o_2080 = 0;
    self->o_207e = 0;
    self->o_207c = 0;
    self->o_207a = 0;
    self->o_20dc = 0;
    self->o_2076 = 0;
    self->o_20e4 = 0;
    self->o_208a = 0;
    self->o_20e0 = 0;
    self->o_2086 = 0;
    self->o_2082 = 0;
    self->o_20ac = 0;
    self->o_20aa = 0;
    self->o_208e = 0;
    self->o_208c = 0;
    for (i = 0; i < 22; i++)
        self->o_20ae[i] = 0;
    for (i = 0; i < 11; i++)
        self->o_2092[i] = 0;
    self->o_20a8 = 0;
    self->o_2088 = 0xaaaa;
    self->o_rate_div100 = (int16_t)(sample_rate / 100);
}
