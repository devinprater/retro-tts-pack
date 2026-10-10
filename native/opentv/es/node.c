/*
 * Taking nodes off the free list and putting them on the work list.
 *
 * Every stage above this works on the same doubly linked list of 618 nodes,
 * so these two are underneath the whole pipeline.  Engine_ResetNodes threads
 * the pool onto the free list between two sentinels and Engine_NodeAlloc
 * takes them off the front one at a time; nothing ever allocates.
 */
#include "es_engine.h"

/* @0x10008bd0 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, int32_t after, int32_t type,
                                   uint8_t value)
{
    Node *n;
    StageCtx *st;

    /* Two checks the 1997 engine does not have.  Engine_Error is a stub --
     * three bytes, "ret 4" -- so they report nothing and change nothing, but
     * they are in the instruction stream and so they are here. */
    if (self->free_nodes <= 0)
        Engine_Error(self, 0x1e);
    if (ref == NULL)
        Engine_Error(self, 0x1f);

    n = Engine_Unlink(self, self->free_head->next);
    self->free_nodes--;
    Engine_InsertBefore(self, n, after == 1 ? ref->next : ref);

    n->flags = (n->flags & ~NODE_TYPE_MASK) | ((uint32_t)type & NODE_TYPE_MASK);
    n->value = value;
    n->arg = 0;
    n->b15 = 0;
    n->flags &= ~0xf8u;

    st = self->stage;
    if (st != NULL) {
        if (st->last == ref && after == 1) {
            st->last = n;
            return n;
        }
        if (st->first == ref && after == 0)
            st->first = n;
    }
    return n;
}

/* Append at the end of the work list, and adopt the new node as stage 0's
 * window wherever that window is still empty. */
/* @0x10008b60 */
Node *TV_THISCALL Engine_AppendNode(Engine *self, int32_t type, int32_t value)
{
    StageCtx *s0 = &self->stage_ctx[0];
    Node *n = Engine_NodeAlloc(self, self->work_tail, 0, type, (uint8_t)value);
    s0->last = n;
    if (s0->scan == NULL)
        s0->scan = n;
    if (s0->ctl == NULL)
        s0->ctl = n;
    if (s0->cur == NULL)
        s0->cur = n;
    if (s0->first == NULL)
        s0->first = n;
    return n;
}

/* Put a node back on the free list and report the neighbour the caller
 * should carry on from -- next when forward is 1, previous otherwise, and
 * NULL when that neighbour was outside the stage's window. */
/* @0x10008dc0 */
Node *TV_THISCALL Engine_NodeFree(Engine *self, Node *n, int32_t forward)
{
    Node *ret;
    StageCtx *st;

    if (n == NULL) {
        Engine_Error(self, 0x20);
        return NULL;
    }
    ret = forward == 1 ? n->next : n->prev;
    st = self->stage;
    if (st != NULL) {
        if (n == st->last) {
            if (st->first == st->last) {
                st->last = NULL;
                self->stage->first = NULL;
            } else {
                st->last = st->last->prev;
            }
            if (forward == 1)
                ret = NULL;
        }
        st = self->stage;
        if (n == st->first) {
            st->first = st->first->next;
            if (forward == 0)
                ret = NULL;
        }
        /* Two fix-ups English does not make: the cursor and the control
         * pointer are also moved off the node being freed. */
        st = self->stage;
        if (n == st->cur)
            st->cur = st->cur->next;
        st = self->stage;
        if (n == st->ctl)
            st->ctl = st->ctl->next;
    }
    Engine_Unlink(self, n);
    Engine_InsertBefore(self, n, self->free_tail);
    self->free_nodes++;
    n->flags = (n->flags & ~1u) | NODE_FREE;
    return ret;
}
