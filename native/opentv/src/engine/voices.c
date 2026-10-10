/*
 * Voices OpenTV adds, past the ten the DLL carries.
 *
 * Every per-voice table in the engine is read as TABLE[voice], and the voice
 * itself travels in the high nibble of track 21 (stage3.c ORs it in there), so
 * the encoding has always had room for sixteen voices while the tables have
 * held ten.  This file is what fills the gap: the accessors below answer from
 * the DLL's tables below TV_STOCK_VOICES and from the definitions here above
 * it, and every read site in the engine goes through them.
 *
 * For voices 0..9 an accessor is exactly the array subscript it replaced, so
 * the corpus is unaffected -- which is the point of doing it this way rather
 * than rewriting the tables.
 *
 * A definition gives only what it changes.  Everything left as TV_V_INHERIT
 * comes from the stock voice it is based on, so no table from the original
 * binary is copied into this file: a voice here is a description of how it
 * differs from one of Centigram's, which is also how the voices it is ported
 * from were described.
 */
#include "engine.h"

/* @0x100b5068 */ extern const int32_t  g_voice_adjust[];
/* @0x100b52c0 */ extern const int32_t  g_synth_breath[];
/* @0x100b52e8 */ extern const int32_t  g_voice_nasal_rate[];
/* @0x100b5310 */ extern const uint8_t  g_voice_p18[];
/* @0x100b5320 */ extern const uint8_t  g_voice_p19[];
/* @0x100b5330 */ extern const uint8_t  g_voice_p20[];
/* @0x100b5340 */ extern const uint8_t  g_voice_p21[];
/* @0x100b5350 */ extern const uint32_t g_voice_pitch[];
/* @0x100b5378 */ extern const uint32_t g_voice_rate_index[];
/* @0x100b53a0 */ extern const uint32_t g_voice_speed[];
/* @0x100b53c8 */ extern const int32_t  g_voice_f4[];
/* @0x100b53f0 */ extern const int32_t  g_voice_f4max[];
/* @0x100b5440 */ extern const int32_t  g_voice_pitch_scale[];

/*
 * Frank, a deep male voice, from the MindMaker TextAssist build of TruVoice.
 *
 * That product put the Centigram engine under a voice layer of its own and
 * shipped eight voices as text files of Klatt-style parameters rather than as
 * a retuned voice block -- its Syn.dll carries Centigram's synthesis tables
 * byte for byte and its Clapi.dll Centigram's voice table, so the voices are
 * built above the engine and not inside it.  Frank.tav is twenty-three
 * parameters, and against Bill -- that product's name for Peter, voice 0 --
 * eleven differ:
 *
 *     F0Def       100       ->  72          the base pitch, in hertz
 *     HeadSize    1.0       ->  1.1         the length of the vocal tract
 *     IntonLevel  1.0       ->  0.7         how far the contour moves
 *     SingF0Rate  1.0       ->  0.508666   |
 *     FricRate    0.508666  ->  0.7         |
 *     Breath      0.0       ->  0.1         |  shaping in that product's own
 *     Rich        0.5       ->  0.65        |  synthesiser, with nowhere to go
 *     LBoostFreq  50        ->  100         |  here: two shelf filters, a
 *     LBoostGain  1.0       ->  9.791484    |  source richness, a noise rate
 *     HBoostFreq  2750      ->  3506        |
 *     HBoostGain  1.0       ->  9.248628   |
 *
 * The first three are the ones this engine has somewhere to put.  A *longer*
 * tract puts every formant down by the reciprocal of its length, and 1/1.1 is
 * 0.9091, so the seven percentage columns go to -9.  The bandwidths go with
 * them, keeping each resonator's Q where Centigram put it.
 *
 * Note that this engine's own `breath` is not that `Breath`: it is a one-tap
 * tilt on the previous output sample (generate.c), not aspiration mixed into
 * the source, so the 0.1 has no conversion into it.
 *
 * Frank.tav also carries what Bill.tav does not: a 225-point `Wave` array, a
 * glottal pulse shape of its own, which four of that product's eight voices
 * have.  This engine has one global pulse table, `g_synth_pulse`, so there is
 * nowhere to put a per-voice shape without first making it per-voice -- and
 * that array is data out of that product rather than a description of it, so
 * it is not reproduced here.  See docs/VOICES.md.
 *
 * `Larynx` is 0.0 for Bill and Frank alike, so what reads as creak in that
 * product comes from the pulse shape and those two shelf boosts, not from a
 * setting.  The jitter and shimmer below are therefore this project's choice
 * and not a port.  The reason for making one: **Peter is the only one of
 * Centigram's ten with both switched off**, so any voice based on him is
 * unnaturally steady, and these take Frank no further than Grandpa Amos, which
 * is as far as Centigram itself went.
 *
 * Frank was chosen over the other seven for a measured reason.  This engine
 * tears at 8 and 11 kHz when a voice's formants are raised far -- see
 * docs/VOICES.md, which has the diagnosis as far as it got -- and how badly
 * follows the boost exactly: Johnny at +47 per cent tore 3811 times at 11 kHz
 * in four seconds, Timmy at +30 tore 1528, Rita and Wendy at +23 about 280,
 * and Frank at **-9 tears not at all**, at any of the three rates.  He is the
 * one of the eight that asks the filter bank for less than Peter does rather
 * than more.
 *
 * A port of a description, not of samples, and whether it sounds like the
 * TextAssist Frank is a question for the ear.
 */
/*
 * TextAssist's Voice Editor has six sliders on its General tab, and its help
 * screen names them, which is what pins these down:
 *
 *     Volume 100  Head size 100  Frication rate 50
 *     Richness 50  Breathiness 0  Creakiness 0        <- Bill, voice 0
 *
 * against Bill.tav's Volume 1.0, HeadSize 1.0, FricRate 0.508666, Rich 0.5,
 * Breath 0.0 and Larynx 0.0.  So `Breath` is **Breathiness** and `Larynx` is
 * **Creakiness**, and Frank reads Breathiness 10, Creakiness 0.
 *
 * Both have a home here already, in the adjustment columns:
 *
 *     adj[12]  added to track 2, the aspiration amplitude -- breathiness
 *     adj[10]  jitter, perturbs the pitch period        *     adj[11]  shimmer, perturbs the source amplitude   |  creakiness
 *
 * Creakiness is therefore **0** for Frank, and for all eight of that product's
 * voices: `Larynx` is 0.0 in every one of the eight .tav files.  Jitter and
 * shimmer stay at Peter's zero here for that reason, though the columns are
 * left named below because a voice of one's own may well want them.
 *
 * Breathiness is 0.1 of that product's scale, and adj[12] turns out to be the
 * wrong home for it.  That column is an *offset* on track 2, and track 2 runs
 * through a curve that is flat zero below index 14, so on a vowel -- whose
 * track 2 is 0 -- adding ten still asks for silence.  Centigram's own ten use
 * adj[12] at 0 to 15 (Peter and Melvin 0, Grandpa Amos 6, Alex 12, Sidney and
 * Wanda 15), which lifts the sounds that are aspirated already and leaves the
 * vowels alone.  That is a different parameter from the one the slider means.
 *
 * So breathiness here is `aspir`, a floor rather than an offset, held for as
 * long as the voice sounds: 76 of track 2's own units, which measured takes
 * the share of energy above 2 kHz from 0.0027 to 0.0048, where 84 would make
 * it 0.0129 and 88 clips.  Frank asks for 10 of 100 where Wendy asks 50, so he
 * belongs at the gentle end of that.
 */
#define FRANK_JITTER  0                 /* Creakiness 0 */
#define FRANK_SHIMMER 0                 /* Creakiness 0 */
#define FRANK_ASPIR   76                /* Breathiness 10 */
#define FRANK_BREATH  TV_V_INHERIT      /* the output tilt, a different thing */

static const TvVoiceDef g_extra_voices[] = {
    {
        "Frank", 0,
        /*  F1%  B1%  F2%  B2%  F3%  B3%  F4% */
        {    -9,  -9,  -9,  -9,  -9,  -9,  -9,
             TV_V_INHERIT,          /*  7  F1 resonator table offset */
             TV_V_INHERIT,          /*  8  frequency ceiling */
             TV_V_INHERIT,          /*  9  nasal resonator offset */
             FRANK_JITTER,          /* 10  jitter depth */
             FRANK_SHIMMER,         /* 11  shimmer depth */
             TV_V_INHERIT,          /* 12  added to track 2 */
             TV_V_INHERIT,          /* 13  track 18 */
             TV_V_INHERIT },        /* 14  track 19 */
        72,                                     /* pitch: F0Def */
        TV_V_INHERIT,                           /* speed */
        TV_V_INHERIT,                           /* rate_index */
        FRANK_BREATH,                           /* breath */
        TV_V_INHERIT,                           /* nasal_rate */
        TV_V_INHERIT,                           /* f4 */
        TV_V_INHERIT,                           /* f4max */
        TV_V_INHERIT,                           /* pitch_scale */
        TV_V_INHERIT, TV_V_INHERIT,             /* p18, p19 */
        TV_V_INHERIT, TV_V_INHERIT,             /* p20, p21 */
        100,                                    /* gain: nothing to hold back */
        70,                                     /* inton: IntonLevel 0.7 */
        FRANK_ASPIR                             /* aspir: Breathiness 10 */
    }
};

const int32_t tv_extra_voice_count =
    (int32_t)(sizeof g_extra_voices / sizeof g_extra_voices[0]);

/* The definition for a voice above the stock ten, or NULL for one of them. */
static const TvVoiceDef *def_of(int32_t v)
{
    if (v < TV_STOCK_VOICES || v >= TV_STOCK_VOICES + tv_extra_voice_count)
        return NULL;
    return &g_extra_voices[v - TV_STOCK_VOICES];
}

const char *tv_extra_voice_name(int32_t v)
{
    const TvVoiceDef *d = def_of(v);
    return d ? d->name : NULL;
}

/*
 * The adjustment row.  A voice of our own is a copy of its base row with the
 * columns it overrides written over it, built once here and handed back; the
 * engine reads it every frame and never writes to it.
 */
const int32_t *tv_v_adjust(int32_t v)
{
    static int32_t row[15];
    const TvVoiceDef *d = def_of(v);
    int i;

    if (d == NULL)
        return &g_voice_adjust[v * 15];
    for (i = 0; i < 15; i++)
        row[i] = d->adjust[i] == TV_V_INHERIT
                 ? g_voice_adjust[d->base * 15 + i]
                 : d->adjust[i];
    return row;
}

/*
 * A custom voice's source level.  The engine multiplies nothing by this for a
 * stock voice, so the path is exactly what it was.
 */
int32_t tv_v_gain(int32_t voice, int32_t v)
{
    const TvVoiceDef *d = def_of(voice);

    if (d == NULL || d->gain == TV_V_INHERIT || d->gain == 100)
        return v;
    return v * d->gain / 100;
}

/* Breathiness: the level aspiration is held at while the voice sounds.  A
 * stock voice is 0 and frame.c does nothing at all. */
int32_t tv_v_aspir(int32_t voice)
{
    const TvVoiceDef *d = def_of(voice);

    return (d == NULL || d->aspir == TV_V_INHERIT) ? 0 : d->aspir;
}

/* How far this voice's contour moves, as a percentage.  A stock voice is 100
 * and the engine multiplies by nothing. */
int32_t tv_v_inton(int32_t voice)
{
    const TvVoiceDef *d = def_of(voice);

    return (d == NULL || d->inton == TV_V_INHERIT) ? 100 : d->inton;
}

/* One scalar table each, so a read site stays as readable as the subscript
 * it replaced. */
#define TV_V_SCALAR(fn, field, table, type)                     \
    int32_t fn(int32_t v)                                       \
    {                                                           \
        const TvVoiceDef *d = def_of(v);                        \
        if (d != NULL && d->field != TV_V_INHERIT)              \
            return d->field;                                    \
        return (int32_t)(type)table[d ? d->base : v];           \
    }

TV_V_SCALAR(tv_v_pitch,       pitch,       g_voice_pitch,       uint32_t)
TV_V_SCALAR(tv_v_rate_index,  rate_index,  g_voice_rate_index,  uint32_t)
TV_V_SCALAR(tv_v_speed,       speed,       g_voice_speed,       uint32_t)
TV_V_SCALAR(tv_v_breath,      breath,      g_synth_breath,      int32_t)
TV_V_SCALAR(tv_v_nasal_rate,  nasal_rate,  g_voice_nasal_rate,  int32_t)
TV_V_SCALAR(tv_v_f4,          f4,          g_voice_f4,          int32_t)
TV_V_SCALAR(tv_v_f4max,       f4max,       g_voice_f4max,       int32_t)
TV_V_SCALAR(tv_v_pitch_scale, pitch_scale, g_voice_pitch_scale, int32_t)
TV_V_SCALAR(tv_v_p18,         p18,         g_voice_p18,         uint8_t)
TV_V_SCALAR(tv_v_p19,         p19,         g_voice_p19,         uint8_t)
TV_V_SCALAR(tv_v_p20,         p20,         g_voice_p20,         uint8_t)
TV_V_SCALAR(tv_v_p21,         p21,         g_voice_p21,         uint8_t)
