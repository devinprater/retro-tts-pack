/*
 * The synthesiser's step.
 *
 * Engine_Step calls this once per pass.  It generates one frame of audio
 * when there is one to generate, clears the flag that said so, and notices
 * when the parameter tracks have run dry.  Then it runs the parameter-list
 * builder whatever happened, which is the function the image's one surviving
 * trace string calls ParL.
 *
 * The three guards at the top are the same three flags Synth_Gate works on,
 * read here rather than through it: the hold from ESC[H, the busy flag, and
 * the one that says a frame is due.
 */
#include "es_engine.h"

/* @0x100077b0 */
uint8_t TV_THISCALL Synth_Step(Engine *self)
{
    if (self->synth_hold == 0 && self->synth_busy == 0 &&
        self->synth_19ad != 0) {
        if (self->w_212e != 0)
            Synth_Generate(self, self->sample_rate, self->filt_coef);
        self->synth_19ad = 0;
        if (self->trk_04 == self->trk_08)
            self->synth_busy = 1;
    }
    Prosody_Build(self);
    return 1;
}

/*
 * The four questions and two edits the rest of the engine puts to the
 * parameter tracks, through one entry point with an operation number.
 *
 *     1   is there room for `arg` more frames before the window fills
 *     2   slide the whole window down by 0x800 when the write cursor has
 *         run past it, which is how the tracks stay in a fixed buffer
 *     3   is the read cursor still behind the write cursor
 *     4   advance the read cursor by one
 *
 * Operation 2 is the interesting one: it subtracts 0x800 from all
 * forty-four per-track cursors and from the four global ones, and it leaves
 * trk_08 alone when it holds -1, which is the value Synth_ResetTracks puts
 * there.  Anything other than 1 to 4 reports error 0x21 and answers no.
 */
/* @0x10017900 */
uint8_t TV_THISCALL Tracks_Op(Engine *self, int32_t op, int32_t arg)
{
    int i;

    switch (op) {
    case 1:
        return (uint8_t)(self->trk_10 - self->trk_04 + self->s3_1fe4 + arg
                         < 0x100);
    case 2:
        if (self->trk_04 > 0x800) {
            for (i = 0; i < 22; i++) {
                self->trk_wr[i] -= 0x800;
                self->trk_rd[i] -= 0x800;
            }
            self->trk_04 -= 0x800;
            if (self->trk_08 != -1)
                self->trk_08 -= 0x800;
            self->trk_0c -= 0x800;
            self->trk_10 -= 0x800;
        }
        return 1;
    case 3:
        return (uint8_t)(self->s3_1fe0 + self->trk_04 < self->trk_0c);
    case 4:
        self->trk_04++;
        return 1;
    default:
        Engine_Error(self, 0x21);
        return 0;
    }
}
