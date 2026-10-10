/*
 * The front of the preformatter.
 *
 * Characters arrive one at a time from Engine_Flush, are tidied and queued in
 * pre_ring, and then Preformat_Run is called until the queue is empty.  The
 * pre_busy flag makes that re-entrant-safe: Preformat_Run can itself end up
 * back here, and when it does the character is only queued.
 *
 * pre_ring is 0x100 bytes, not 0x1000 like the other two, which is why the
 * free count wraps at 0x100 and the write index is masked with 0xff.
 */
#include "es_engine.h"

/* @0x10008060 */
void TV_THISCALL Preformat_PutChar(Engine *self, uint8_t c)
{
    int32_t n;

    if (!self->s2_1d55 && self->free_nodes > 0x260)
        self->s2_1d54 = 1;

    if (c == '\r' || c == '\n' || c == '\t')
        c = ' ';
    if (c == 0 || c == 0x7f)
        return;
    if (c < 0x20 && c != 0x1b)
        return;

    n = self->pre_rd - self->pre_wr - 1;
    if (n < 0)
        n += 0x100;
    if (n <= 0)
        return;

    self->pre_ring[self->pre_wr] = c;
    self->pre_wr = (self->pre_wr + 1) & 0xff;

    if (self->pre_busy)
        return;
    self->pre_busy = 1;
    while (self->pre_rd != self->pre_wr)
        Preformat_Run(self);
    self->pre_busy = 0;
}
