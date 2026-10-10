/*
 * Singing: a score of notes compiled into the escapes the engine understands.
 *
 * The syntax is the one MindMaker's TextAssist used, which is DECtalk's:
 *
 *     PHONEME<duration_ms,pitch>
 *
 * written in the engine's own one-character-per-phoneme alphabet -- the same
 * alphabet tvtts_speak_phonemes takes, so "hello" is `HeLO`.  That product
 * spelled it `[:phone arpa TruVoice]` at the head of a score, but the `arpa` is
 * a fiction: these are not two-letter ARPABET names, and the likeliest reason
 * for the word is that its users had come from the Creative Labs build and
 * expected DECtalk's spelling.
 *
 * **The engine itself does not implement any of this.**  Handed
 * `H<100,20>e<600,20>-L<100,20>O<1500,20>` -- 2300 ms of notes -- CGRM_EN.DLL
 * renders 1.16 s, barely longer than plain "hello", because `phonetic.c` gives
 * `<` and `>` unrelated meanings and throws away what is between them.  That
 * product parsed the score in its own layer and drove the synthesiser directly,
 * which is why it ships Syn.dll split out from Clapi.dll.  So this file is
 * OpenTV's, not a decompilation, and it works by compiling a score down to two
 * escapes the engine does understand.
 *
 * The pitch scale is DECtalk's, established by playing with the original:
 *
 *     0          a rest
 *     1 to 37    a chromatic scale, C2 (65 Hz) up to C5 (523 Hz)
 *     38 and up  hertz, directly, which is what lets a score glide
 *
 * Note 1 being low C is the same 65.41 Hz that turns up as the Spanish
 * engine's pitch clamp: this family of engines counts from low C.
 */
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"
#include "tvtts_port.h"

#define ESC 27

/*
 * A note longer than this has to be sung as repeats of its phoneme.  The limit
 * is not arbitrary: stage 3 asks `Tracks_Op(self, 1, n)`, which is
 * `... + n < 0x100`, so a duration has to fit the track window.  Past 60
 * hundredths the engine stops lengthening and starts wrapping -- measured, a
 * forced 200 came out *shorter* than a forced 60.
 */
#define SING_MAX_CS   60
/* The longest silence one pause escape can ask for: its argument is a byte. */
#define SING_MAX_REST_MS (255 * SING_MS_PER_CS)
/* And the longest one hold escape can ask for; its argument is a byte too. */
#define SING_MAX_HOLD_MS (255 * SING_MS_PER_CS)
/*
 * What the *last* rest of an utterance loses, and it loses it every time.
 *
 * Measured, a rest with a note after it is exact -- 300 ms asked gives 299,
 * 500 gives 499, 1000 gives 998, 2000 gives 1995 -- and the same rest with
 * nothing after it comes up a flat 390 ms short whatever its length: 300 asked
 * gives -90, 500 gives 110, 1000 gives 609, 2000 gives 1606.  So it is the
 * utterance ending, not the pitch moving, and the engine is trimming the
 * silence it was about to end on.
 *
 * An earlier reading of this had it the other way about -- exact with nothing
 * after it, short when the pitch changed -- and the compensation was spent
 * accordingly, on whichever hold happened to come before a pitch change.  Both
 * halves of that were wrong.  It is added back here, where it is lost.
 */
#define SING_END_REST_MS 390

/*
 * How long a rest lasts when the score does not say.
 *
 * A duration of 0 means "not specified" rather than "no time" -- DECtalk's own
 * reader says so in as many words, `user_durs[n]` being "User-specified dur if
 * non-zero" -- and the phoneme then takes the length its rules give it.  Its
 * rules give a silence `dpause` of 14 or 15 frames depending on what follows,
 * floored at 2 and scaled by the speech rate, which at 6.4 ms a frame is about
 * 90 to 96 ms.
 *
 * So a hundred, which is also what the demo's own `_<0,100>` says if its two
 * arguments are read the other way round.  Both readings land in the same
 * place, which is a comfortable thing for a guess to do.
 */
#define SING_REST_DEFAULT_MS 100

/*
 * A note too long for one phoneme is held by the engine rather than repeated;
 * see the hold escape where it is written.  This is the longest one phoneme
 * can carry on its own.
 */
#define SING_MAX_NOTE_MS (SING_MAX_CS * SING_MS_PER_CS)

/*
 * What a phoneme costs before its held part begins.  Measured through this
 * very escape, by holding one duration over strings of five and twenty-five
 * phonemes and differencing, which cancels what the utterance costs at its
 * ends.  Straight and flat the whole way:
 *
 *      cs      1     5    12    25    40    60
 *      ms   20.5  60.4 130.2 259.9 409.6 609.1
 *
 * so **ms = 10 * cs + 10**.  An earlier reading of this said 30 rather than
 * 10, and it was wrong: it came from an instrumented build that forced the
 * duration on *every* node, boundaries included, rather than on the phonemes
 * a score names.  Twenty milliseconds a note does not sound like much and is
 * three seconds across a song.
 */
/*
 * A phoneme lasts **10 ms per hundredth, exactly**, with nothing added -- but
 * only if the duration escape is emitted immediately before it.
 *
 * That proviso is the whole thing, and it cost three wrong models to find.
 * Measured over strings of differing phonemes:
 *
 *      cs                  3      5      8     20     30     45
 *      one escape      13.72  33.67  63.61 183.33 283.11 432.77
 *      escape each     29.93  49.89  79.82 199.55 299.32 448.98
 *
 * The second row is 10*cs to within a fifth of a millisecond.  The first is
 * **16.2 ms lower at every value** -- one escape node's worth, once per
 * phoneme, and the difference does not vary with the duration at all.
 *
 * So the compiler emits the escape before every phoneme rather than only when
 * the value changes.  Skipping the repeats looked like an obvious economy and
 * quietly shortened every note that happened to match its predecessor.
 *
 * (Measuring a string of the *same* phoneme instead gives 10*cs + 10, which is
 * a third answer again: identical neighbours have no transition to overlap.
 * Measure the mechanism in the shape it will be used in.)
 */
#define SING_ONSET_MS 0
#define SING_MS_PER_CS 10

int TVTTS_CALL tvtts_note_hz(int note)
{
    /* C2 up to C5.  65.41 * 2^((n-1)/12), rounded; 22 is A3 at 220 and 34 is
     * A4 at 440, which is the arithmetic checking itself. */
    static const short tbl[37] = {
          65,   69,   73,   78,   82,   87,   /* 1   C2  .. 6   F2  */
          93,   98,  104,  110,  117,  123,   /* 7   F#2 .. 12  B2  */
         131,  139,  147,  156,  165,  175,   /* 13  C3  .. 18  F3  */
         185,  196,  208,  220,  233,  247,   /* 19  F#3 .. 24  B3  */
         262,  277,  294,  311,  330,  349,   /* 25  C4  .. 30  F4  */
         370,  392,  415,  440,  466,  494,   /* 31  F#4 .. 36  B4  */
         523                                  /* 37  C5             */
    };

    if (note <= 0)
        return 0;
    if (note <= 37)
        return tbl[note - 1];
    return note;                    /* 38 and above are already hertz */
}

/* Somewhere to build the compiled score, growing as it goes. */
typedef struct {
    char   *buf;
    size_t  cap;
    size_t  len;                    /* what was wanted, which may exceed cap */
} out;

static void put(out *o, char c)
{
    if (o->buf != NULL && o->len < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void put_num(out *o, int v)
{
    char d[12];
    int n = 0;

    if (v <= 0) {
        put(o, '0');
        return;
    }
    while (v > 0 && n < (int)sizeof d) {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n > 0)
        put(o, d[--n]);
}

/* ESC [ <v> <letter> */
static void put_esc(out *o, int v, char letter)
{
    put(o, ESC);
    put(o, '[');
    put_num(o, v);
    put(o, letter);
}

static void put_str(out *o, const char *s)
{
    while (*s != '\0')
        put(o, *s++);
}

/* Read digits, stopping at the first byte that is not one. */
static int read_num(const char **p)
{
    int v = 0;

    while (**p >= '0' && **p <= '9') {
        if (v < 1000000)
            v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v;
}

static int pitch_escape(int hz)
{
    /*
     * For a pitch written as hertz rather than a note -- 38 and above, which is
     * what a glide uses -- there is no table entry, so the engine's own
     * flatness is corrected by ratio.  Measured at both rates the rendered
     * frequency is about 0.975 of the pitch asked for, and that holds across
     * rates because pitch is in hertz and not in samples.  Thousandths, so the
     * arithmetic stays integer.
     */
    return (hz * 1000 / 975 + 1) / 2;
}

/*
 * MindMaker's phoneme alphabet against this engine's.
 *
 * They are very nearly the same -- "hello" is `HeLO` in both -- which is why
 * the demo's strings render identically through the original DLL and through
 * this port.  That agreement proves only that the two *engines* agree, not that
 * a character means what the score intended, and two of them do not exist here
 * at all: fed `6` or `7` the engine emits **nothing**.  The score needs both,
 * in "morni-ng" and "dru-nken".
 *
 * Aligning the score against what this engine's own tvtts_text_to_phonemes
 * gives for the same words settles them:
 *
 *      morning   score  M g N 6 7        ours  M g N | ~ p
 *      drunken   score  D R v 7 K @ 7    ours  J z R v ~ K | N
 *
 * so `6` is `|` and `7` is `~`.  Measured: five copies of `6` or `7` render as
 * silence, five of `|` or `~` render 253 and 221 ms each, and `MgN67` comes out
 * 1060 ms against `MgN|~`'s 1658 -- the two missing sounds.
 */
static char tv_phoneme(char c)
{
    switch (c) {
    case '6': return '|';
    case '7': return '~';
    default:  return c;
    }
}

/*
 * Two durations the engine will not take, found by sweeping every value from 1
 * to 60 and comparing against 10*cs:
 *
 *   - **12 renders about 420 ms instead of 120.**  Every other value from 2 to
 *     60 is exact to a fifth of a millisecond; 12 behaves as though it were 42,
 *     wherever it appears and whatever its neighbours are.  In the sailor this
 *     was worth +299 ms at every note written `<120,...>` -- six of them, and
 *     most of that song's overshoot.
 *   - **1 crashes the engine.**
 *
 * Why 12 is special is not known.  Since the cost of avoiding it is at most ten
 * milliseconds and the cost of using it is three hundred, the compiler steps
 * around both and takes whichever neighbour is nearer what was asked.
 */
static int safe_cs(int cs, int ms)
{
    if (cs < 2)
        return 2;
    if (cs == 12)
        return (ms < 120) ? 11 : 13;
    return cs;
}

/* The hundredths that come nearest a length in milliseconds, avoiding the two
 * values the engine will not take. */
static int dur_cs(int ms)
{
    int cs;

    if (ms > SING_MAX_CS * SING_MS_PER_CS + SING_ONSET_MS)
        ms = SING_MAX_CS * SING_MS_PER_CS + SING_ONSET_MS;
    cs = (ms - SING_ONSET_MS + SING_MS_PER_CS / 2) / SING_MS_PER_CS;
    if (cs > SING_MAX_CS)
        cs = SING_MAX_CS;
    return safe_cs(cs, ms);
}

/*
 * Which escape to write for each note of the scale, measured rather than
 * calculated -- and one table for every sample rate.
 *
 * The engine turns pitch into a whole number of sample periods, so only certain
 * frequencies exist at all, and the escape carries half the pitch, which thins
 * them further.  Rendering a held tone at every escape from 25 to 243 and
 * measuring what came out shows how coarse the grid really is:
 *
 *      near 125 Hz   27.3 cents between neighbouring escapes
 *      near 195 Hz   30.4
 *      near 300 Hz   47.5
 *      near 450 Hz   70.7
 *
 * No formula beats the grid, and the flat ratio that was here before did worse
 * than the grid allows, because it landed wherever the arithmetic fell.  That
 * is what makes a scale sound out of tune: not the average error but its
 * *unevenness*, every note off by a different amount and so every interval
 * wrong.  This names, for each note, the escape measured nearest to true equal
 * temperament.
 *
 * **One table, not one per rate.**  The pitch an escape asks for is in hertz,
 * not samples, so the same escape means the same note at any rate; only the
 * rounding to a whole period differs.  Fitting a table to each rate separately
 * tunes each one marginally better and makes them disagree with *each other*
 * more, which is worse than useless -- a score should not change key when it is
 * rendered at a different rate.  Measured over notes 1 to 35:
 *
 *                        11025      16000     rates apart
 *      a table each    11.1 c      9.4 c      11.3 c  (worst 41.8)
 *      one table       11.1 c     10.3 c       7.1 c
 *
 * which costs nine tenths of a cent at 16 kHz and halves the disagreement.
 *
 * The top of the table saturates, because this escape goes into a byte holding
 * half the pitch and the grid up there is coarse -- 11025 over 24, 23 and 22 is
 * 459, 479 and 501 Hz and nothing between.  It no longer decides what a note
 * sings: the `q` escape below carries the pitch in quarter-hertz, straight to
 * the period, and B4 and C5 are in tune from it whatever this table says.
 * Measured at every note from 31 to 37, changing this value changes neither the
 * pitch nor the level by anything at all.  See docs/SINGING.md.
 */
static const unsigned char g_note_esc[37] = {
     35,  37,  39,  41,  43,  46,  48,  51,  54,  57,
     61,  64,  68,  71,  76,  80,  85,  90,  94, 101,
    107, 113, 120, 126, 134, 140, 148, 160, 170, 176,
    189, 199, 208, 225, 238, 242, 242
};

/* ESC [ <a> ; <b> <letter> */
static void put_esc2(out *o, int a, int b, char letter)
{
    put(o, ESC);
    put(o, '[');
    put_num(o, a);
    put(o, ';');
    put_num(o, b);
    put(o, letter);
}

/*
 * A note's pitch in quarter-hertz, from true equal temperament with A4 at 440.
 * Kept as a table of exact quarter-hertz so no floating point is needed and the
 * freestanding build stays as it is: note 1 is C2 at 65.406 Hz, which is 262
 * quarter-hertz, and note 34 is A4 at exactly 1760.
 */
static int note_quarter_hz(int note, int hz)
{
    static const short q[37] = {
         262,  277,  294,  311,  330,  349,  370,  392,  415,  440,
         466,  494,  523,  554,  587,  622,  659,  698,  740,  784,
         831,  880,  932,  988, 1047, 1109, 1175, 1245, 1319, 1397,
        1480, 1568, 1661, 1760, 1865, 1976, 2093
    };

    if (note >= 1 && note <= 37)
        return q[note - 1];
    return hz * 4;              /* a pitch written as hertz */
}

int TVTTS_CALL tvtts_sing_compile(const char *score, char *buf, size_t cap)
{
    const char *p = score;
    out o;
    int cur_hz = -1;                /* what the last pitch escape asked for */
    char last_ph = 0;               /* for a tie to hold */
    /*
     * `H` cannot stand on its own.  It is aspiration on the onset of the vowel
     * after it, and an escape node between the two throws it away: with a
     * duration escape sitting between the H and the e, `HeLO1` and `eLO1` come
     * out byte-identical, while with nothing between them they differ -- and
     * plain `HeLO1` matches the spoken word "hello" to the byte.  So an H is
     * held back and written immediately before the phoneme it belongs to, with
     * its length added to that phoneme's.
     */
    int carry_h = 0, carry_ms = 0;
    int pending_rest = 0;           /* a rest waiting for its note */
    int cur_cs = -1;                /* and the last duration */

    if (score == NULL)
        return -1;
    o.buf = buf;
    o.cap = cap;
    o.len = 0;

    put_str(&o, "\033[1I");         /* the engine's phoneme mode */

    while (*p != '\0') {
        char ph = *p;
        int ms = 0, note = -1, spec = 0;


        /*
         * Layout, a space included.  A space in a score separates words for
         * whoever is reading it; every note already says how long it lasts, so
         * a space must cost nothing.  Left in, the engine treats it as a word
         * boundary and puts a pause of its own choosing there -- and that pause
         * is *variable*, which wrecks a rhythm far more thoroughly than it
         * wrecks a total.  Perturbing one note at a time showed it: notes away
         * from a space answered a +100 ms change to within 0.2 ms, while the
         * note after a space answered +399 and the one before it -50.
         *
         * An earlier attempt at this concluded the opposite.  At the time each
         * note was 26 ms short, and the pauses were quietly making up the
         * difference, so removing them looked like a regression.
         */
        if (ph == ' ' || ph == '\n' || ph == '\r' || ph == '\t') {
            p++;
            continue;
        }
        p++;

        if (*p == '<') {
            spec = 1;
            p++;
            ms = read_num(&p);
            if (*p == ',') {
                p++;
                note = read_num(&p);
            }
            if (*p == '>')
                p++;
        }

        /*
         * `-` is a syllable tie in that product's notation, not a sound, and
         * this engine renders it as silence -- so `He-LO` puts a gap in the
         * middle of "hello".  Hold the phoneme before it instead, which is what
         * a tie means and what keeps the bar the length it was written.
         */
        if (ph == '-') {
            /*
             * A bare `-` is a syllable mark and nothing else -- the demo writes
             * `e<600,20>-L<100,20>`, with no length on the dash at all -- so it
             * must cost nothing.  Given one, it is a tie and holds the phoneme
             * before it for that long.
             *
             * Letting a bare one through cost the four "hello"s 0.53 s between
             * them: with no length of its own it fell to the duration rules and
             * sounded the previous phoneme again for however long they liked.
             */
            if (!spec || last_ph == 0)
                continue;
            ph = last_ph;
        }

        /*
         * `_` is a rest.  Like `-` it is that product's notation rather than a
         * phoneme -- it is not in TextAssist's own code table either -- and
         * this engine has no character for it, so written through it rendered
         * as nothing at all: `A<300,20>_<1000,0>A<300,20>` came out *shorter*
         * than the two notes alone.
         *
         * The engine does have a pause, and in phoneme mode its argument is
         * simply hundredths of a second of silence.  Measured: ESC[10s gives
         * 100 ms, ESC[30s 299, ESC[49s 489, ESC[69s 688 -- ten milliseconds a
         * step with no offset.  (tvtts_break_sequence adds 49 to its argument,
         * which is right for the text path and wrong for this one.)
         */
        if (ph == '_') {
            /*
             * A rest with no length of its own gets the default one.  The
             * sailor asks for exactly that twice, as `_<0,100>`, at the two
             * places its verses break -- and until this was here those two
             * rests were nothing at all, so the song ran its verses together.
             *
             * The pitch on a rest is read and dropped either way: there is
             * nothing to sound, so `_<1000,0>` and `_<0,100>` differ only in
             * how long they are silent for.
             */
            pending_rest += (ms > 0) ? ms : SING_REST_DEFAULT_MS;
            last_ph = 0;            /* nothing here for a tie to hold */
            continue;
        }

        /*
         * An explicit zero length on a *phoneme* is zero, not "as long as the
         * rules like": left to them it became a sound of the engine's choosing
         * in the middle of a bar.  A rest is the exception and is handled just
         * above, because for a rest DECtalk's reading of a zero is the one that
         * matters -- see SING_REST_DEFAULT_MS.
         *
         * The demo never writes a zero length on anything but a rest, so what a
         * sung `X<0,n>` ought to be has not had to be decided.  DECtalk would
         * give it the phoneme's own rule duration at pitch n.
         */
        if (spec && ms == 0)
            continue;

        /*
         * A pitch of 0 means the score is not singing this one, only timing it:
         * DECtalk reads it that way too -- `user_f0[n]` is "User-specified f0 if
         * non-zero", and only a non-zero value turns its singing mode on at all.
         *
         * **Not honoured on a phoneme**, and deliberately not faked.  Handing
         * the exact pitch back is one escape, and it changes nothing audible:
         * the coarse pitch escape has already moved the engine's *base* pitch to
         * the last note, and the duration escape has already gated the contour
         * off, so the phoneme comes out flat at the previous note's pitch either
         * way -- measured, the period histogram is the same to a single period.
         * Doing it properly needs two things this layer has not got: a way to
         * force a duration without gating the contour, which is an engine
         * change, and the pitch the caller configured, which only the port
         * knows.  The demo never writes a pitch of 0 on anything but a rest, so
         * nothing in it is waiting on this.
         */

        if (note >= 0) {
            int hz = tvtts_note_hz(note);

            /* A rest keeps whatever pitch was in force: there is nothing to
             * hear, and changing it would only cost an escape. */
            /*
             * Always, like the duration escape and for the same reason: an
             * escape node costs the phoneme after it time, so emitting one
             * only when the pitch changes makes a note's length depend on
             * whether it happens to repeat its predecessor's note.  The sailor
             * changes note constantly and ran 6.9 per cent long against hello's
             * 2.6 short, purely from that.
             */
            if (hz > 0 && hz != cur_hz) {
                /*
                 * The escape carries half the pitch (see
                 * tvtts_pitch_sequence), and TVTTS_EXT_SING is what lets it
                 * past 400 Hz.
                 *
                 * The four per cent is measured, not invented.  With the
                 * contour gated the engine sings a note consistently flat --
                 * 3.0 per cent at note 20, 4.6 at note 12, 7.3 at the bottom
                 * of the scale -- and half of the worst of that is the
                 * escape's own halving throwing away a hertz at 65 Hz.  The
                 * note table keeps returning true musical frequencies; the
                 * correction belongs here, where the engine is being spoken
                 * to.
                 */
                /*
                 * A note of the scale comes from the measured table; a pitch
                 * written as hertz (38 and up, which is what a glide uses) has
                 * no table entry and keeps the ratio correction.
                 */
                put_esc(&o, (note >= 1 && note <= 37)
                            ? g_note_esc[note - 1]
                            : pitch_escape(hz), 'p');
                /*
                 * And then the pitch exactly, in quarter-hertz, which the
                 * engine turns into a fractional period.  The escape above
                 * still goes out: everything downstream of the pitch track --
                 * the source amplitude among it -- is left reading what it
                 * always did, and only the period itself is pinned.
                 *
                 * A note is sent as true equal temperament rather than
                 * anything the grid can reach, because with a fractional
                 * period there is no grid left to round to.
                 */
                {
                    int q = note_quarter_hz(note, hz);

                    /*
                     * A pitch written out in hertz is marked, because it is
                     * what a glide is written with: the engine then travels to
                     * it over the whole phoneme instead of over 100 ms, and
                     * does not waver on the way.  DECtalk draws the same line
                     * -- its vibrato switch is set for a note from the table
                     * and cleared for a straight line.
                     */
                    if (!(note >= 1 && note <= 37))
                        q |= 0x4000;
                    put_esc2(&o, q & 0xff, (q >> 8) & 0xff, 'q');
                }
                cur_hz = hz;
            }
        }

        /*
         * The silence goes here: after the pitch escape for the note that
         * follows it, never before.  A pitch escape *immediately after* a
         * pause truncates it -- measured, a 1000 ms rest came out 998 ms with
         * nothing after it and 609 with a pitch change after it, every time.
         * Nothing sounds during a rest, so setting the pitch first costs
         * nothing and keeps the silence its full length.
         */
        if (pending_rest > 0) {
            while (pending_rest > 0) {
                int take = (pending_rest > SING_MAX_REST_MS)
                           ? SING_MAX_REST_MS : pending_rest;

                put_esc(&o, (take + SING_MS_PER_CS / 2) / SING_MS_PER_CS, 's');
                pending_rest -= take;
            }
            cur_cs = -1;            /* the pause disturbs the held duration */
        }

        /* An H waits for its vowel; see carry_h above. */
        if (tv_phoneme(ph) == 'H') {
            carry_h = 1;
            carry_ms += ms;
            last_ph = ph;
            continue;
        }

        if (ms > 0 || carry_h) {
            int left = ms + carry_ms;

            /*
             * An H sounds only when nothing separates it from its vowel -- and
             * when nothing does, the duration escape before the pair applies to
             * *both* of them, so `[60d He` costs 1197 ms rather than 600.
             * Measured every way round:
             *
             *     [60d e            598 ms
             *     [60d He          1197 ms   <- two phonemes, not one
             *     [60d H [10d e     100 ms   <- H silently thrown away
             *
             * So write the H and one vowel's worth together at the H's own
             * length, then let the vowel make up whatever it still owes.  The
             * pair then costs exactly what the score asked for and the
             * aspiration survives.
             */
            if (carry_h) {
                int hcs = dur_cs(carry_ms > 0 ? carry_ms : SING_MS_PER_CS);

                put_esc(&o, hcs, 'd');
                cur_cs = hcs;
                put(&o, 'H');
                put(&o, tv_phoneme(ph));
                /* the escape fed both, so the pair has sounded twice over */
                left -= 2 * hcs * SING_MS_PER_CS;
                if (left < 0)
                    left = 0;
                carry_h = 0;
                carry_ms = 0;
                /*
                 * And whatever the vowel still owes is *held*, not said again.
                 *
                 * Letting it fall through to the ordinary emission below wrote
                 * a second copy of the vowel, which is a re-articulation the
                 * score never asked for: `H<120,20>A<600,20>` in the sailor
                 * said its vowel twice -- "hey-ey" -- and `H<100,20>e<600,20>`
                 * did the same to "hello".  The pair is 2 x hcs of the note and
                 * the hold carries the rest of it.
                 */
                while (left >= SING_MS_PER_CS) {
                    int take = (left > SING_MAX_HOLD_MS)
                               ? SING_MAX_HOLD_MS : left;

                    put_esc(&o, (take + SING_MS_PER_CS / 2) / SING_MS_PER_CS,
                            'g');
                    left -= take;
                    cur_cs = -1;
                }
                left = 0;
            }

            /*
             * One phoneme carries as much of the note as stage 3 allows --
             * sixty hundredths -- and the engine holds the rest.
             *
             * Singing the remainder as *more of the same phoneme* was built
             * twice and does not work in this engine; see docs/SINGING.md for
             * the measurements.  The engine articulates across every pair of
             * phonemes, so each join comes out as a re-articulation, and
             * suppressing it means holding the parameters -- which needs a
             * frame to hold, and no rule for choosing one held up across
             * phonemes.
             *
             * The hold is written immediately, not deferred.  The deferral
             * existed only to pay the 390 ms a following pitch change used to
             * cost, and measured on this build it costs nothing: a 500 ms hold
             * delivers 1098 ms whether or not the pitch moves after it.
             */
            if (left > 0) {
                int cs = dur_cs(left > SING_MAX_NOTE_MS
                                ? SING_MAX_NOTE_MS : left);

                put_esc(&o, cs, 'd');
                cur_cs = cs;
                put(&o, tv_phoneme(ph));
                left -= cs * SING_MS_PER_CS;
            }
            while (left >= SING_MS_PER_CS) {
                int take = (left > SING_MAX_HOLD_MS) ? SING_MAX_HOLD_MS : left;

                put_esc(&o, (take + SING_MS_PER_CS / 2) / SING_MS_PER_CS, 'g');
                left -= take;
                cur_cs = -1;        /* the hold disturbs the held duration */
            }
            last_ph = ph;
        } else {
            /* No duration asked for, so hand this one back to the duration
             * rules -- ESC[0d is how they are turned off again. */
            if (cur_cs != 0) {
                put_esc(&o, 0, 'd');
                cur_cs = 0;
            }
            if (carry_h) {
                put(&o, 'H');
                carry_h = 0;
                carry_ms = 0;
            }
            put(&o, tv_phoneme(ph));
            last_ph = ph;
        }
    }

    if (pending_rest > 0)           /* and the last one is short by a flat 390 */
        pending_rest += SING_END_REST_MS;
    while (pending_rest > 0) {      /* a rest with nothing after it */
        int take = (pending_rest > SING_MAX_REST_MS)
                   ? SING_MAX_REST_MS : pending_rest;

        put_esc(&o, (take + SING_MS_PER_CS / 2) / SING_MS_PER_CS, 's');
        pending_rest -= take;
    }

    if (carry_h)
        put(&o, 'H');               /* nothing followed it, so on its own */

    put_esc2(&o, 0, 0, 'q');        /* and give the pitch back to the engine */
    put_str(&o, "\033[0d");         /* leave the rules as they were found */
    put_str(&o, "\033[0I");
    put(&o, '\0');
    return (int)o.len;
}


/* ---- the phoneme-input command ------------------------------------------- */

/*
 * `[:phone TruVoice on]` and `[:phone TruVoice off]`.
 *
 * The engine has a phoneme-input mode and `ESC[1I` / `ESC[0I` is how it is
 * turned on and off -- `tvtts_speak_phonemes` does nothing but wrap a string
 * in that pair, which is what `tts_SpeakPhoneme` did in the 5.1 builds.  What
 * it has not got is a way to ask for it from inside the *text*, and that is
 * what a product spells `[:phone ...]`.
 *
 * TextAssist's own demo writes `[:phone arpa TruVoice]`, and the `arpa` is a
 * fiction: these are one-character phonemes, not two-letter ARPABET names.
 * The likeliest reason for the word is that its users had come from the
 * Creative Labs build and expected DECtalk's spelling.  So the form here says
 * what it means -- the alphabet is TruVoice's, and the argument is whether
 * phoneme input is on.
 *
 * Anything that is not exactly this command is left alone, byte for byte, so
 * text that merely happens to contain a bracket still reads as text.
 *
 * Reachable from the API, from `tv`, and from the speak window.  NVDA and the
 * SAPI driver filter inline commands out of the text before it ever arrives
 * here, so it does not reach the engine by those two routes.
 */

static int ci_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    return a == b;
}

/* `word` at text[i..len), ignoring case.  Returns the index just past it, or
 * 0 for no match -- 0 is never a valid answer here because i is always past
 * the opening bracket. */
static size_t match_word(const char *t, size_t len, size_t i, const char *word)
{
    size_t k = 0;

    while (word[k] != 0) {
        if (i + k >= len || !ci_eq(t[i + k], word[k]))
            return 0;
        k++;
    }
    return i + k;
}

static size_t skip_blank(const char *t, size_t len, size_t i)
{
    while (i < len && (t[i] == ' ' || t[i] == '\t'))
        i++;
    return i;
}

/*
 * Where one word is written as two: `CamelCase` said as `Camel Case`.
 *
 * The engine has no notion of it.  A capital inside a word reaches the
 * letter-to-sound rules and changes nothing, so `CamelCase` renders byte for
 * byte what `camelcase` renders and the two words run together.  NVDA splits
 * such words above its drivers, which is why the add-on sounded right where the
 * speak window did not, so this belongs in the library: `tv`, the speak window
 * and the SAPI voice then all agree.
 *
 * Returns how many spaces the text wants; with `buf` non-NULL it writes the
 * split text there, which needs `len` plus that many bytes.
 *
 * **It has to run after `tv_phone_commands`, not before.**  Before, it splits
 * the command's own name -- `[:phone Tru Voice on]` matches nothing -- and then
 * splits the phoneme text the command introduces, where a capital names a
 * different phoneme: `HeLO` became `He LO`.  Afterwards there is one thing to
 * avoid instead of two, and it is keyed off a single byte: an escape.  Nothing
 * inside `ESC [ ... <letter>` is touched, and `ESC[1I` and `ESC[0I` are watched
 * going past, because between them the text is phonemes rather than words.
 */
size_t tv_camel_split(const char *t, size_t len, char *buf)
{
    size_t i, added = 0;
    int phone = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)t[i];

        if (c == ESC) {
            /* copy the escape whole, and note a switch of input mode */
            int val = 0, seen = 0;

            while (i < len) {
                c = (unsigned char)t[i];
                if (buf != NULL)
                    buf[i + added] = (char)c;
                if (c >= '0' && c <= '9') {
                    val = val * 10 + (c - '0');
                    seen = 1;
                } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                    if (c == 'I')
                        phone = seen ? (val != 0) : 0;
                    break;              /* the letter ends it */
                }
                i++;
            }
            continue;
        }

        /*
         * A capital after a lowercase letter or a digit starts a word.  So does
         * a capital that follows another capital and is followed by a lowercase
         * letter, which is what keeps `HTMLParser` from coming out as
         * `H T M L Parser`; an all-capitals word has no such letter after it and
         * stays whole.
         */
        if (!phone && c >= 'A' && c <= 'Z' && i > 0) {
            unsigned char prev = (unsigned char)t[i - 1];
            unsigned char next = (i + 1 < len) ? (unsigned char)t[i + 1] : 0;
            int prev_low = (prev >= 'a' && prev <= 'z') ||
                           (prev >= '0' && prev <= '9');
            int prev_up = (prev >= 'A' && prev <= 'Z');
            int next_low = (next >= 'a' && next <= 'z');

            if (prev_low || (prev_up && next_low)) {
                if (buf != NULL)
                    buf[i + added] = ' ';
                added++;
            }
        }
        if (buf != NULL)
            buf[i + added] = (char)c;
    }
    return added;
}

size_t tv_phone_commands(char *text, size_t len)
{
    size_t r = 0, w = 0;

    while (r < len) {
        size_t i, j;
        int on;

        /*
         * Every way out of this is the same: copy one byte and carry on from
         * the next, which leaves a bracket that begins nothing untouched.
         */
        if (text[r] != '[' || r + 1 >= len || text[r + 1] != ':')
            goto literal;
        i = skip_blank(text, len, r + 2);
        if ((j = match_word(text, len, i, "phone")) == 0)
            goto literal;
        i = skip_blank(text, len, j);
        if (i == j)                             /* a space has to follow */
            goto literal;
        if ((j = match_word(text, len, i, "truvoice")) == 0)
            goto literal;
        i = skip_blank(text, len, j);
        if (i == j)
            goto literal;
        if ((j = match_word(text, len, i, "off")) != 0)
            on = 0;
        else if ((j = match_word(text, len, i, "on")) != 0)
            on = 1;
        else
            goto literal;
        i = skip_blank(text, len, j);
        if (i >= len || text[i] != ']')
            goto literal;

        /*
         * Four bytes for the twenty the shortest spelling takes, so writing
         * back over the same buffer can never overtake what is being read.
         */
        text[w++] = (char)ESC;
        text[w++] = '[';
        text[w++] = on ? '1' : '0';
        text[w++] = 'I';
        r = i + 1;
        continue;

literal:
        text[w++] = text[r++];
    }
    return w;
}
