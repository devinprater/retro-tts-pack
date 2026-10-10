/*
 * The preformatter proper: the "ESC [ ... <letter>" command set.
 *
 * It is a three-state machine over the characters queued in pre_ring.  State
 * 1 is ordinary text, which is folded and copied to mid_ring; ESC moves to
 * state 2, which expects '[' and moves to state 3; state 3 collects decimal
 * parameters separated by ';' until a letter arrives, and that letter selects
 * a command.  Commands that the pipeline below needs to know about are
 * re-emitted into mid_ring in a binary form -- ESC, the letter, a count, then
 * that many bytes -- which is what Engine_InputStage reads back.
 *
 * Twenty-three letters, and they are the same twenty-three the 1997 English
 * engine has.  What differs is smaller and in two places: the defaults ESC[w
 * restores are 0x1780 and 0x40 against English's 0x17c0 and 0x41, one flag
 * bit having been added in 1997; and mode_I here has a companion flag that
 * ESC[..I sets and the text path consults, which English does not.
 */
#include <stddef.h>          /* offsetof, for the one raw-offset write */

#include "es_engine.h"

/* @0x1004990c */
extern const uint32_t g_bit_for_param[17];
/* @0x10049760 */
extern const int32_t g_rate_class_max[10];
/* @0x100613c8 */
extern const uint8_t g_default_params[22];
/* @0x10061358 */
extern const int32_t g_param_max[22];

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* @0x10007810 */
void TV_THISCALL Preformat_Run(Engine *self)
{
    uint8_t c = self->pre_ring[self->pre_rd];
    int32_t *param = self->esc_param;
    int32_t cnt, p0, nargs, k;
    uint32_t mask, v;

    self->pre_rd = (self->pre_rd + 1) & 0xff;
    if (c == 0x1b) {
        self->esc_state = 2;
        return;
    }
    switch (self->esc_state) {
    case 1: {
        uint8_t second = c, base;
        if (self->skip_text)
            return;
        /* In index mode the byte goes through untouched; this check is the
         * 1995 engine's and the 1997 one has nothing like it. */
        if (self->mode_I_on) {
            Engine_MidPut(self, c);
            return;
        }
        base = Accent_Split(&second);
        if (base == 0)
            return;
        Engine_MidPut(self, base);
        if (second != 0)
            Engine_MidPut(self, second);
        return;
    }
    case 2:
        if (c == '[') {
            self->esc_state = 3;
            self->esc_overflow = 0;
            self->esc_nparam = 16;
            do {
                self->esc_nparam--;
                param[self->esc_nparam] = -1;
            } while (self->esc_nparam > 0);
        } else {
            self->esc_state = 1;
        }
        return;
    case 3:
        break;
    default:
        return;
    }

    if (c >= '0' && c <= '9') {
        /*
         * The -1 test happens before the bounds test, so with more than
         * sixteen parameters the original writes past the array into whatever
         * engine field lies there.  Reproduced by offset rather than by
         * indexing, as the English decompilation does it.
         *
         * The offset is taken from the field and not written as 0x1f0, which
         * is where the field sits only while pointers are four bytes wide.
         * This engine reaches here on every digit of every escape sequence --
         * not just past the sixteenth parameter, the way English's does -- so a
         * literal offset sends every ESC[<digit> into the wrong field on a
         * 64-bit build, and sixteen corpus cases heard it.  Where it lands
         * *past* the array still differs between the two word widths, because
         * what follows the array is not the same distance away; nothing but
         * more than sixteen parameters can reach that, and no input here does.
         */
        Engine_ZeroDwordIfMinus1(self, (uint32_t)offsetof(Engine, esc_param) +
                                       4u * self->esc_nparam);
        if (self->esc_nparam >= 16)
            return;
        param[self->esc_nparam] = param[self->esc_nparam] * 10 + c - '0';
        if (param[self->esc_nparam] > 255)
            self->esc_overflow = 1;
        return;
    }
    if (c == ';') {
        self->esc_nparam++;
        if (self->esc_nparam < 16)
            param[self->esc_nparam] = 0;
        return;
    }

    /* a letter: run the command */
    nargs = -1;
    cnt = self->esc_nparam;
    if (cnt != 0 || param[0] != -1)
        cnt++;
    if (self->esc_overflow || cnt > 16)
        c = 0;
    p0 = param[0];
    switch (c) {
    case 'A':
    case 'D':
        mask = 0;
        for (k = 0; k < cnt; k++)
            if (param[k] >= 1 && param[k] <= 7)
                mask |= g_bit_for_param[param[k]];
        if (c == 'A')
            self->flags_A |= mask;
        else
            self->flags_A &= ~mask;
        v = (uint32_t)self->flags_A;
        nargs = 2;
        c = 'A';
        p0 = v & 0xff;
        param[1] = (v >> 8) & 0xff;
        break;
    case 'N':
    case 'F':
        mask = 0;
        for (k = 0; k < cnt; k++)
            if (param[k] >= 1 && param[k] <= 16)
                mask |= g_bit_for_param[param[k]];
        if (c == 'N')
            self->flags_N |= mask;
        else
            self->flags_N &= ~mask;
        v = (uint32_t)self->flags_N;
        nargs = 2;
        c = 'N';
        p0 = v & 0xff;
        param[1] = (v >> 8) & 0xff;
        break;
    case 'C':
        if (cnt != 0)
            break;
        self->synth_hold = 0;
        nargs = 0;
        break;
    case 'H':
        if (cnt != 0)
            break;
        self->synth_hold = 1;
        nargs = -2;
        break;
    case 'I':
        if (cnt > 1)
            break;
        if (p0 < 0)
            p0 = 0;
        if (p0 <= 1) {
            nargs = 1;
            self->mode_I = p0;
        }
        /* Written even when the parameter was out of range and the command
         * did nothing else: the jump that rejects p0 > 1 lands after the
         * mode_I store and before this one, so ESC[2I still turns index
         * mode off.  Only visible if it was on, which is why the unit case
         * runs every string twice. */
        self->mode_I_on = (p0 == 1);
        break;
    case 'P':
        if (cnt > 1)
            break;
        if (p0 < 0)
            p0 = 1;
        else if (p0 > 1)
            break;
        nargs = 1;
        self->mode_P = p0;
        break;
    case 'S':
        if (cnt != 0)
            break;
        self->reset_pending = 1;
        nargs = -2;
        break;
    case 'V':
        if (cnt > 1)
            break;
        p0 = clamp(p0, 0, 9);
        nargs = 1;
        self->voice = p0;
        break;
    case 'a':
        if (cnt > 1)
            break;
        p0 = clamp(p0, 0, 16);
        nargs = 1;
        self->volume_atten = p0;
        break;
    case 'c':
        if (cnt > 1)
            break;
        if (p0 < 0 || p0 > 2)
            p0 = 0;
        nargs = 1;
        break;
    case 'f':
        if (cnt > 1)
            break;
        p0 = clamp(p0, 0, 9);
        self->rate_class = p0;
        if (self->rate_index >= g_rate_class_max[p0])
            self->rate_index = g_rate_class_max[p0];
        nargs = 2;
        param[1] = self->rate_index;
        break;
    case 'g':
    case 's':
        if (cnt > 1)
            break;
        p0 = p0 < 0 ? 100 : clamp(p0, 1, 255);
        nargs = 1;
        break;
    case 'i':
        if (cnt > 4)
            break;
        if (p0 < 0) {
            nargs = 1;
            p0 = 1;
        } else {
            nargs = cnt;
        }
        break;
    case 'l':
        if (p0 == -1 || cnt > 2 || p0 >= 22)
            break;
        if (param[1] < 0)
            param[1] = g_default_params[p0];
        else if (g_param_max[p0] < param[1])
            param[1] = g_param_max[p0];
        nargs = 2;
        self->cfg_bytes[p0] = (uint8_t)param[1];
        break;
    case 'p':
        if (cnt > 1)
            break;
        p0 = p0 < 0 ? 42 : clamp(p0, 25, 200);
        nargs = 1;
        self->pitch = p0 * 2;
        break;
    case 'r':
    case 'v':
        if (c == 'r') {
            if (cnt > 1)
                break;
            if (p0 < 50 && p0 > 0)
                p0 = 50;
            self->speed_wpm = p0 > 0 ? p0 : 150;
            if (p0 > 0)
                p0 = (p0 - 46) >> 3;
        }
        if (cnt > 1)
            break;
        if (p0 < 0)
            p0 = 13;
        else if (p0 > 25)
            p0 = 25;
        if (self->rate_class != 0 && p0 > g_rate_class_max[self->rate_class])
            p0 = g_rate_class_max[self->rate_class];
        nargs = 1;
        self->rate_index = p0;
        if (c == 'v')
            self->speed_wpm = p0 * 8 + 50;
        break;
    case 't':
        if (cnt > 3)
            break;
        nargs = 3;
        if (p0 < 0)
            p0 = 0;
        else if (p0 >= 10)
            nargs = -1;
        param[1] = param[1] < 0 ? 100 : clamp(param[1], 1, 255);
        param[2] = param[2] < 0 ? 50 : clamp(param[2], 30, 243);
        break;
    case 'w':
        if (cnt != 0)
            break;
        self->mode_I = 0;
        self->mode_P = 1;
        self->rate_index = 13;
        self->speed_wpm = 150;
        self->pitch = 85;
        self->volume_atten = 0;
        self->rate_class = 0;
        self->flags_N = 0x1780;
        self->flags_A = 0x40;
        self->voice = 0;
        for (k = 0; k < 5; k++) {
            StageCtx *s = &self->stage_ctx[k];
            s->p_1c = self->mode_I;
            s->p_20 = self->mode_P;
            s->rate_index = self->rate_index;
            s->pitch = self->pitch;
            s->volume_atten = self->volume_atten;
            s->p_30 = self->rate_class;
            s->p_34 = self->flags_N;
            s->p_38 = self->flags_A;
            s->voice = self->voice;
        }
        nargs = -2;
        break;
    case 'x':
        if (cnt != 0)
            break;
        self->stop_mark = 1;
        self->synth_hold = 0;
        nargs = 0;
        break;
    default:
        nargs = -1;
        break;
    }

    param[0] = p0;
    if (nargs != -2 && !self->skip_text && Engine_MidFree(self) >= nargs + 3) {
        Engine_MidPut(self, 0x1b);
        Engine_MidPut(self, c);
        Engine_MidPut(self, (uint8_t)nargs);
        for (k = 0; k < nargs; k++)
            Engine_MidPut(self, (uint8_t)param[k]);
    }
    self->esc_state = 1;
}
