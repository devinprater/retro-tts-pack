/*
 * Doubly linked list surgery, used by the node allocator and by every stage
 * that moves a node around.
 *
 * Both are member functions that never touch the engine: the original takes
 * `this` in ecx as thiscall requires and then ignores it, reading its real
 * arguments off the stack.  The self parameter is kept here so the calling
 * convention matches what the callers in the DLL expect.
 *
 * Neither checks anything.  The lists always have sentinels at both ends, so
 * next and prev are never null for a node that is actually on one -- which is
 * also why passing a sentinel to Engine_NodeAlloc with after == 1 walks off
 * the end, as the unit tests found out the hard way.
 */
#include "es_engine.h"

/* Take n out of whatever list it is on.  n's own next and prev are left
 * pointing at its old neighbours -- the allocator overwrites both before
 * anyone reads them, so clearing them here would sound identical.  The
 * unit case still rejects it, which is the point of comparing the whole
 * object: the job is to be the same function, not an equivalent one. */
/* @0x10009110 */
Node *TV_THISCALL Engine_Unlink(Engine *self, Node *n)
{
    (void)self;
    n->prev->next = n->next;
    n->next->prev = n->prev;
    return n;
}

/* @0x10008ed0 */
Node *TV_THISCALL Engine_InsertBefore(Engine *self, Node *n, Node *before)
{
    (void)self;
    n->prev = before->prev;
    n->prev->next = n;
    n->next = before;
    before->prev = n;
    return n;
}

/* Not a function of the original: the escape parser's digit handler tests
 * param[n] for -1 before it checks whether n is inside the array, so with
 * more than sixteen parameters it writes into whatever engine field follows.
 * Doing that by offset keeps the behaviour without indexing out of bounds,
 * which is how the English decompilation handles the same line. */
void Engine_ZeroDwordIfMinus1(Engine *self, uint32_t off32)
{
    int32_t *p = (int32_t *)((uint8_t *)self + off32);

    if (off32 + 4 <= sizeof(Engine) && *p == -1)
        *p = 0;
}
