/*
 * Voices of OpenTV's own, for the 1995 Spanish engine.
 *
 * The mechanism is the English engine's, mirrored rather than shared, because
 * the two generations keep their per-voice data in differently named tables.
 * See src/engine/voices.c, whose comments explain the shape; what follows is
 * what differs.
 *
 * A voice above the stock ten is a definition that says only what it changes.
 * Everything left ES_V_INHERIT is read from the stock voice it is based on, so
 * a definition stays short and says what it means.  For a voice below ten every
 * accessor here returns the subscript it replaced, which is what keeps the
 * corpus byte-identical.
 *
 * The row the adjustment table is read with is track 21's high nibble, and
 * lang/spa/engine/stage3.c ORs `st->voice << 4` into it, so the nibble *is* the voice index.
 * Four bits of it: a language cannot have more than sixteen voices, and the
 * stock ten leave room for six.
 */
#include "es_engine.h"

extern const int32_t g_voice_pitch[];
extern const int32_t g_voice_rate_index[];
extern const int32_t g_voice_speed[];
extern const int32_t g_1004c8a0[];
extern const uint8_t g_1004c7e8[16];
extern const uint8_t g_1004c7f8[16];
extern const uint8_t g_1004c808[16];
extern const uint8_t g_1004c818[16];
extern const int32_t g_1004c7c0[];
/* the fifteen-wide adjustment rows, and the one scalar beside them */
extern const int32_t g_voice_adj[];
extern const int32_t g_voice_c[];

/*
 * Francisco: Frank's voice, speaking Spanish.
 *
 * Frank is the deep male voice of MindMaker's TextAssist, built above the
 * English engine -- see src/engine/voices.c and docs/VOICES.md for where his
 * numbers come from and how each was measured.  The two engines take the same
 * fifteen-wide adjustment row and the same pitch units, so the definition
 * carries over unchanged; Pedro stands in for Peter as the voice it is based
 * on, being this engine's first and its plainest male.
 *
 * He is called Francisco rather than Frank so that he is a Spanish name among
 * Spanish names, and so that he does not collide with Franco, which is what the
 * Italian engine calls its Alex.
 */
#define FRANCISCO_JITTER  0             /* Creakiness 0, as Frank.tav has it */
#define FRANCISCO_SHIMMER 0
#define FRANCISCO_ASPIR   76            /* Breathiness 10 of 100 */

static const EsVoiceDef g_extra_voices[] = {
    {
        "Francisco", 0,                 /* based on Pedro */
        /*  F1%  B1%  F2%  B2%  F3%  B3%  F4% */
        {    -9,  -9,  -9,  -9,  -9,  -9,  -9,
             ES_V_INHERIT,          /*  7  F1 resonator table offset */
             ES_V_INHERIT,          /*  8  frequency ceiling */
             ES_V_INHERIT,          /*  9  nasal resonator offset */
             FRANCISCO_JITTER,      /* 10  jitter depth */
             FRANCISCO_SHIMMER,     /* 11  shimmer depth */
             ES_V_INHERIT,          /* 12  added to track 2 */
             ES_V_INHERIT,          /* 13  track 18 */
             ES_V_INHERIT },        /* 14  track 19 */
        72,                             /* pitch, against Pedro's 85 */
        ES_V_INHERIT,                   /* speed */
        ES_V_INHERIT,                   /* rate_index */
        ES_V_INHERIT,                   /* c8a0 */
        ES_V_INHERIT, ES_V_INHERIT,     /* p18, p19 */
        ES_V_INHERIT, ES_V_INHERIT,     /* p20, p21 */
        ES_V_INHERIT,                   /* c7c0 */
        ES_V_INHERIT,                   /* voice_c */
        100,                            /* gain */
        70,                             /* inton */
        FRANCISCO_ASPIR                 /* aspir */
    }
};

const int32_t es_extra_voice_count =
    (int32_t)(sizeof g_extra_voices / sizeof g_extra_voices[0]);

/* The definition for a voice above the stock ten, or NULL for one of them. */
static const EsVoiceDef *def_of(int32_t v)
{
    if (v < ES_STOCK_VOICES || v >= ES_STOCK_VOICES + es_extra_voice_count)
        return NULL;
    return &g_extra_voices[v - ES_STOCK_VOICES];
}

const char *es_extra_voice_name(int32_t v)
{
    const EsVoiceDef *d = def_of(v);
    return d ? d->name : NULL;
}

/*
 * The adjustment row: a copy of the base row with the columns the definition
 * overrides written over it, built once and handed back.  The engine reads it
 * every frame and never writes to it.
 */
const int32_t *es_v_adjust(int32_t v)
{
    static int32_t row[15];
    const EsVoiceDef *d = def_of(v);
    int i;

    if (d == NULL)
        return &g_voice_adj[v * 15];
    for (i = 0; i < 15; i++)
        row[i] = d->adjust[i] == ES_V_INHERIT
                 ? g_voice_adj[d->base * 15 + i]
                 : d->adjust[i];
    return row;
}

/*
 * A custom voice's source level.  The engine multiplies nothing by this for a
 * stock voice, so the path is exactly what it was.
 */
int32_t es_v_gain(int32_t voice, int32_t v)
{
    const EsVoiceDef *d = def_of(voice);

    if (d == NULL || d->gain == ES_V_INHERIT || d->gain == 100)
        return v;
    return v * d->gain / 100;
}

/* Breathiness: the level aspiration is held at while the voice sounds.  A
 * stock voice is 0 and the frame code does nothing at all. */
int32_t es_v_aspir(int32_t voice)
{
    const EsVoiceDef *d = def_of(voice);

    return (d == NULL || d->aspir == ES_V_INHERIT) ? 0 : d->aspir;
}

/* How far this voice's contour moves, as a percentage.  A stock voice is 100
 * and the engine multiplies by nothing. */
int32_t es_v_inton(int32_t voice)
{
    const EsVoiceDef *d = def_of(voice);

    return (d == NULL || d->inton == ES_V_INHERIT) ? 100 : d->inton;
}

/* One scalar table each, so a read site stays as readable as the subscript it
 * replaced. */
#define ES_V_SCALAR(fn, field, table, type)                     \
    int32_t fn(int32_t v)                                       \
    {                                                           \
        const EsVoiceDef *d = def_of(v);                        \
        if (d != NULL && d->field != ES_V_INHERIT)              \
            return d->field;                                    \
        return (int32_t)(type)table[d ? d->base : v];           \
    }

ES_V_SCALAR(es_v_pitch,      pitch,      g_voice_pitch,      int32_t)
ES_V_SCALAR(es_v_rate_index, rate_index, g_voice_rate_index, int32_t)
ES_V_SCALAR(es_v_speed,      speed,      g_voice_speed,      int32_t)
ES_V_SCALAR(es_v_c8a0,       c8a0,       g_1004c8a0,         int32_t)
ES_V_SCALAR(es_v_p18,        p18,        g_1004c7e8,         uint8_t)
ES_V_SCALAR(es_v_p19,        p19,        g_1004c7f8,         uint8_t)
ES_V_SCALAR(es_v_p20,        p20,        g_1004c808,         uint8_t)
ES_V_SCALAR(es_v_p21,        p21,        g_1004c818,         uint8_t)
ES_V_SCALAR(es_v_c7c0,       c7c0,       g_1004c7c0,         int32_t)
ES_V_SCALAR(es_v_voice_c,    voice_c,    g_voice_c,          int32_t)
