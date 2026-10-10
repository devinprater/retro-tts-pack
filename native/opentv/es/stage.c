/*
 * Stage windows.
 *
 * Each of the five stages owns a window into the one work list: first and
 * last bound it, cur marks how far the stage has finished, ctl is the next
 * control node to execute, and scan is the next node of a type this stage
 * cares about.  A stage brackets its work with Engine_StageBegin and
 * Engine_StageEnd, and between those two self->stage points at it, which is
 * how everything below knows whose window it is walking.
 *
 * Engine_StageEnd is where a node moves down the pipeline: whatever the
 * stage finished becomes the next stage's window, except in stage 4, which
 * has nowhere to pass work to and frees it instead.
 */
#include "es_engine.h"

/* @0x10049950 */
extern const uint32_t g_node_type_bits[8];

/* Does this stage care about this node's type?  English writes the mask test
 * inline at each of its three uses; here it is a function, and it takes the
 * null node its callers would otherwise have to check for. */
/* @0x10009020 */
uint8_t TV_THISCALL Engine_TypeSelected(Engine *self, Node *n)
{
    if (n == NULL)
        return 0;
    return (self->stage->type_mask & g_node_type_bits[NODE_TYPE(n)]) != 0;
}

/* @0x10008ca0 */
Node *TV_THISCALL Engine_StagePrev(Engine *self, Node *n)
{
    if (n == NULL) {
        Engine_Error(self, 0x2b);
        return NULL;
    }
    if (self->stage->first == n)
        return NULL;
    return n->prev;
}

/* @0x10008ea0 */
Node *TV_THISCALL Engine_StageNext(Engine *self, Node *n)
{
    if (n == NULL) {
        Engine_Error(self, 0x2a);
        return NULL;
    }
    if (self->stage->last == n)
        return NULL;
    return n->next;
}

/* Take the window, advance scan to the next node of interest, and execute
 * any control nodes sitting in front of it.  Returns whether there is
 * anything for the stage to do. */
/* @0x10009050 */
uint8_t TV_THISCALL Engine_StageBegin(Engine *self, StageCtx *st)
{
    Node *n, *c;

    self->stage = st;
    n = st->scan;
    while (n != NULL) {
        if (Engine_TypeSelected(self, n))
            break;
        n = Engine_StageNext(self, n);
    }
    self->stage->scan = n;

    for (;;) {
        /* the type test comes first here and copes with a null node; English
         * tests for null first and then the mask */
        if (Engine_TypeSelected(self, self->stage->ctl))
            break;
        if (self->stage->ctl == NULL)
            break;
        if (!Engine_RunControl(self))
            break;
        st = self->stage;
        c = st->ctl;
        st->ctl = Engine_StageNext(self, c);
        if (self->stage->cur == c)
            self->stage->cur = self->stage->ctl;
    }
    return n != NULL;
}

/* Hand everything the stage finished to the stage below, and give up the
 * window.  Returns whether anything moved. */
/* @0x10008cd0 */
uint8_t TV_THISCALL Engine_StageEnd(Engine *self)
{
    StageCtx *st = self->stage;
    StageCtx *nx = st + 1;
    Node *first = st->first;
    Node *cur, *done_last;
    uint8_t moved = 0;

    if (first != NULL && st->cur != first) {
        cur = st->cur;
        moved = 1;
        done_last = cur != NULL ? cur->prev : st->last;
        if (st != &self->stage_ctx[4]) {
            nx->last = done_last;
            if (nx->scan == NULL)
                nx->scan = self->stage->first;
            if (nx->ctl == NULL)
                nx->ctl = self->stage->first;
            if (nx->cur == NULL)
                nx->cur = self->stage->first;
            if (nx->first == NULL)
                nx->first = self->stage->first;
        } else if (cur != first) {
            /* nothing below stage 4: what it finished goes back to the pool */
            do {
                StageCtx *s = self->stage;
                s->first = Engine_NodeFree(self, s->first, 1);
            } while (self->stage->cur != self->stage->first);
        }
        st = self->stage;
        if (st->cur == NULL) {
            st->first = NULL;
            self->stage->ctl = NULL;
            self->stage->scan = NULL;
            self->stage->last = NULL;
        } else {
            st->first = st->cur;
        }
    }
    self->stage = NULL;
    return moved;
}
