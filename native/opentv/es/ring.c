/*
 * The engine's character rings.
 *
 * in_ring receives raw text from the SAPI TextData items (Engine_Feed); the
 * preformat stage moves characters from it into mid_ring, which the first
 * pipeline stage consumes.  Both keep one slot empty, so "free" is
 * rd - wr - 1 modulo the ring size.
 *
 * These eight are the same functions as the English engine's, and one of
 * them proves it: Engine_InGet and Engine_MidGet are identical code over
 * different rings, which is why tools/xmatch.py scores each of them 1.00
 * against both and cannot tell them apart.  What separates them is the
 * displacement -- 0x4cbf reads in_ring, 0x5dcf reads mid_ring.
 */
#include "es_engine.h"

/* @0x1000e5a0 */
int32_t TV_THISCALL Engine_InFree(Engine *self)
{
    int32_t n = self->in_rd - self->in_wr - 1;
    if (n < 0)
        n += TV_ES_RING_SIZE;
    return n;
}

/* @0x1000e5c0 */
int32_t TV_THISCALL Engine_InGet(Engine *self)
{
    int32_t rd = self->in_rd;
    int32_t c;
    if (self->in_wr == rd)
        return -1;
    c = self->in_ring[rd];
    rd++;
    self->in_rd = rd;
    if (rd >= TV_ES_RING_SIZE)
        self->in_rd = 0;
    return c;
}

/* Step the read position back one character (undo Engine_InGet). */
/* @0x1000e600 */
uint8_t TV_THISCALL Engine_InUnget(Engine *self)
{
    if (Engine_InFree(self) <= 0)
        return 0;
    self->in_rd--;
    if (self->in_rd < 0)
        self->in_rd += TV_ES_RING_SIZE;
    return 1;
}

/* Append one character, keeping 10 slots in reserve for the end markers.
 * A right single quotation mark (cp1252 0x92) is stored as an apostrophe --
 * the same fold the English engine does. */
/* @0x1000e630 */
uint8_t TV_THISCALL Engine_InPut(Engine *self, uint8_t c)
{
    if (Engine_InFree(self) < 10)
        return 0;
    if (c == 0x92)
        c = '\'';
    self->in_ring[self->in_wr] = c;
    self->in_wr++;
    if (self->in_wr >= TV_ES_RING_SIZE)
        self->in_wr = 0;
    return 1;
}

/* Terminate the input stream: "ESC[0i" then "ESC[C" and two spaces. */
/* @0x1000e680 */
uint8_t TV_THISCALL Engine_InPutEnd(Engine *self)
{
    static const uint8_t end_markers[] = {0x1b, '[', '0', 'i', 0x1b, '[', 'C', ' ', ' '};
    size_t i;
    if (Engine_InFree(self) < 10)
        return 0;
    for (i = 0; i < sizeof end_markers; i++)
        Engine_InPut(self, end_markers[i]);
    return 1;
}

/* @0x1000e4f0 */
int32_t TV_THISCALL Engine_MidFree(Engine *self)
{
    int32_t n = self->mid_rd - self->mid_wr - 1;
    if (n < 0)
        n += TV_ES_RING_SIZE;
    return n;
}

/* @0x1000e510 */
int32_t TV_THISCALL Engine_MidGet(Engine *self)
{
    int32_t rd = self->mid_rd;
    int32_t c;
    if (self->mid_wr == rd)
        return -1;
    c = self->mid_ring[rd];
    rd++;
    self->mid_rd = rd;
    if (rd >= TV_ES_RING_SIZE)
        self->mid_rd = 0;
    return c;
}

/* @0x1000e550 */
uint8_t TV_THISCALL Engine_MidPut(Engine *self, uint8_t c)
{
    if (Engine_MidFree(self) < 1)
        return 0;
    self->mid_ring[self->mid_wr] = c;
    self->mid_wr++;
    if (self->mid_wr >= TV_ES_RING_SIZE)
        self->mid_wr = 0;
    return 1;
}
