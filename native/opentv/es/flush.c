/*
 * Moving pending input on towards the pipeline.
 *
 * With TextIn enabled the characters go through the tokenizer object; with
 * it off they go straight into the preformatter, at most 400 per call so the
 * caller's loop keeps getting a turn.  new_item is set for the first flush of
 * a new TextData item, which is what makes the tokenizer start over.
 */
#include "es_engine.h"

/* @0x1001c710 */
void TV_THISCALL Engine_Flush(Engine *self, int32_t new_item)
{
    TextIn *ti = self->textin;
    int32_t n, c;

    if (self->textin_on && ti != NULL) {
        ti->input_done = self->st_input_empty;
        ti->item_done = self->item_done;
        /* the original tests the low byte, not the whole argument */
        if ((uint8_t)new_item)
            TextIn_Reset(ti);
        TextIn_Flush(ti, 0);
        return;
    }
    for (n = 0;;) {
        c = Engine_InGet(self);
        if (c < 0) {
            self->st_input_empty = 1;
            break;
        }
        n++;
        Preformat_PutChar(self, (uint8_t)c);
        if (n >= 400)
            break;
    }
    if (n > 0)
        self->st_idle = 0;
}
