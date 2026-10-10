/*
 * Input stage: turns the preformatted character stream in mid_ring into work
 * list nodes, which is where the pipeline proper begins.
 *
 * It stops after ten characters, or at the first non-space after a space, so
 * the stages below it get a turn between words rather than after the whole
 * item.  e_9180 holds the one character of lookahead that survives across
 * calls; -1 means empty.
 *
 * The preformatter re-emits "ESC [ ... <letter>" commands in a binary form --
 * ESC, the letter, a length, then that many bytes -- and read_control turns
 * one of those back into a control node.  The length selects the shape of
 * what follows, which is the jump table at 0x1000e4d4.
 */
#include "es_engine.h"

/* Inlined into Engine_InputStage by the original's compiler; kept separate
 * here because it is a whole idea of its own.  gcc inlines it back. */
static void read_control(Engine *self)
{
    Node *n = Engine_AppendNode(self, NODE_CONTROL, 0x1b);
    int32_t v;

    n->value = (uint8_t)Engine_MidGet(self);
    switch (Engine_MidGet(self)) {
    case 1:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = 0;
        if (n->value == 'i')
            n->notify = self->item_notify;
        break;
    case 2:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = (uint8_t)Engine_MidGet(self);
        if (n->value == 'A')
            self->in_flags_A = ((int32_t)n->b15 << 8) | (uint8_t)n->arg;
        if (n->value == 'N')
            self->in_flags_N = ((int32_t)n->b15 << 8) | (uint8_t)n->arg;
        break;
    case 3:
        /* first byte carries flag bits for the node, then a 2-byte argument */
        v = Engine_MidGet(self);
        n->flags = (n->flags & ~0x18u) | (((uint32_t)v << 3) & 0x18u);
        if (v & 4)
            n->flags |= 0x20;
        if (v & 8)
            n->flags |= 0x40;
        n->arg = (uint32_t)Engine_MidGet(self);
        n->b15 = (uint8_t)Engine_MidGet(self);
        break;
    case 4:
        n->arg = (uint32_t)Engine_MidGet(self);
        n->arg |= (uint32_t)Engine_MidGet(self) << 8;
        n->arg |= (uint32_t)Engine_MidGet(self) << 16;
        n->arg |= (uint32_t)Engine_MidGet(self) << 24;
        if (n->value == 'i')
            n->notify = self->item_notify;
        break;
    default:
        n->arg = 0;
        n->b15 = 0;
        break;
    }
}

/* @0x1000e290 */
uint8_t TV_THISCALL Engine_InputStage(Engine *self)
{
    int32_t count = 0;
    uint8_t saw_space = 0;
    uint8_t appended = 0;
    int32_t c;
    Node *n;

    if (self->e_9180 == -1) {
        self->e_9180 = Engine_MidGet(self);
        if (self->e_9180 == -1)
            return 0;
    }
    for (;;) {
        c = self->e_9180;
        if (c == ' ')
            saw_space = 1;
        else if (saw_space)
            break;
        if (count++ >= 10)
            break;

        if (c == 0x1b) {
            read_control(self);
        } else if (c == '[' && (self->in_flags_A & 0x20) && !(self->in_flags_N & 3)) {
            n = Engine_AppendNode(self, NODE_CONTROL, 'I');
            n->arg = (self->in_flags_A & 0x40) ? 2 : 1;
        } else if (c == ']' && (self->in_flags_A & 0x20) && !(self->in_flags_N & 3)) {
            n = Engine_AppendNode(self, NODE_CONTROL, 'I');
            n->arg = 0;
        } else {
            Engine_AppendNode(self, 1, c);
        }

        self->e_9180 = -1;
        appended = 1;
        self->e_9180 = Engine_MidGet(self);
        if (self->e_9180 == -1)
            break;
    }
    return appended;
}

/* MSVC's character table and the flag that says whether the multi-byte path
 * has to be taken instead; the engine expands isalpha and friends inline and
 * only calls _isctype for a code page above 1. */
/* @0x1006b638 */
extern const tv_ref g_ctype;
/* @0x1006b844 */
extern int32_t g_mb_codepage;
/* @0x10024093 */
extern int32_t TV_CDECL tv_isctype(int32_t c, int32_t mask);

#define CT_ALPHA  0x103
#define CT_LOWER  0x002
#define CT_ALNUM  0x107

static int32_t feed_ctype(int32_t c, int32_t mask)
{
    if (g_mb_codepage > 1)
        return tv_isctype(c, mask);
    return (int32_t)TV_REF(uint16_t, g_ctype)[c] & mask;
}

/*
 * Take one TextData item from the host and put it into the input ring.
 *
 * With the SAPI "PreFormat" option off the text goes in as it stands, a
 * character at a time, until the ring says no more.  With it on the item is
 * first cut into up to twenty lines -- on CR, LF or CRLF -- and each line is
 * measured as it goes: an ESC[ sequence is skipped up to and including the
 * letter that ends it, and the line is marked as having no words if it holds
 * nothing alphanumeric, and as all capitals if it holds nothing lower case.
 * ESC[1I, which turns index mode on, also clears the capitals mark.
 *
 * Then each line goes into the ring, with trailing spaces and tabs trimmed
 * and, for a line under 40 characters that does not already end in one, a
 * space and a full stop appended, so that the prosody treats it as a sentence.
 * An empty line closes the previous sentence with a full stop instead.  Every
 * line ends with a newline.
 *
 * `*ppos` is how far into the item the last call got; it comes back updated,
 * and the item is only finished -- InPutEnd, item_done -- when the whole of it
 * fitted.
 */
/* @0x1001c310 */
void TV_THISCALL Engine_Feed(Engine *self, const char *text, uint32_t len,
                             uint32_t *ppos)
{
    uint32_t p = *ppos;
    InputSeg *seg;
    int16_t nseg = 0;
    int16_t i;

    if (self->preformat == 0) {
        if (p < len) {
            for (;;) {
                if (!Engine_InPut(self, (uint8_t)text[p]))
                    break;
                p++;
                if (p >= len)
                    break;
            }
        }
        if (p >= len) {
            Engine_InPutEnd(self);
            self->item_done = 1;
        }
        if (*ppos < p)
            self->st_input_empty = 0;
        *ppos = p;
        return;
    }

    if (p == 0)
        self->feed_pos = 0;
    seg = &self->in_seg[0];
    seg->len = 0;
    seg->text = (char *)text + self->feed_pos;
    seg->no_words = 1;
    seg->caps_only = 1;
    seg->flag_c = 0;

    if (self->feed_pos < len) {
        for (;;) {
            uint32_t at = self->feed_pos;
            const char *q = text + at;
            uint8_t c = (uint8_t)*q;

            if (c == '\n' || c == '\r') {
                self->feed_pos = at + 1;
                if (c == '\n') {
                    if (text[at + 1] == '\r')
                        self->feed_pos = at + 2;
                } else if (text[at + 1] == '\n') {
                    self->feed_pos = at + 2;
                }
                /* trailing spaces and tabs are not part of the line */
                while (seg->len > 0) {
                    uint8_t d = (uint8_t)seg->text[seg->len - 1];

                    if (d != ' ' && d != '\t')
                        break;
                    seg->len--;
                }
                nseg++;
                if (nseg >= 0x14)
                    goto trimmed;
                seg = &self->in_seg[nseg];
                seg->len = 0;
                seg->text = (char *)text + self->feed_pos;
                seg->no_words = 1;
                seg->caps_only = 1;
                seg->flag_c = 0;
            } else if (c == 0x1b && q[1] == '[') {
                self->feed_pos = at + 2;
                seg->len = (int16_t)(seg->len + 2);
                seg->no_words = 0;
                {
                    const char *r = text + self->feed_pos;

                    if (r[0] == '1' && r[1] == 'I')
                        seg->caps_only = 0;
                }
                while (self->feed_pos < len) {
                    int32_t alpha =
                        feed_ctype((int32_t)(int8_t)text[self->feed_pos],
                                   CT_ALPHA);

                    self->feed_pos++;
                    seg->len++;
                    if (alpha)
                        break;
                }
            } else {
                int32_t sc = (int32_t)(int8_t)c;

                seg->len++;
                if (feed_ctype(sc, CT_LOWER))
                    seg->caps_only = 0;
                if (feed_ctype(sc, CT_ALNUM))
                    seg->no_words = 0;
                self->feed_pos++;
            }
            if (self->feed_pos >= len)
                break;
        }
    }

    if (nseg != 0x14) {
        while (seg->len > 0) {
            uint8_t d = (uint8_t)seg->text[seg->len - 1];

            if (d != ' ' && d != 0 && d != '\t')
                break;
            seg->len--;
        }
        if (seg->len > 0)
            nseg++;
    }

trimmed:
    for (i = 0; i < nseg; i++) {
        InputSeg *s = &self->in_seg[i];
        int16_t n = s->len;

        if (n > 0) {
            int16_t k;

            self->fmt = 1;
            for (k = 0; k < n; k++)
                Engine_InPut(self, (uint8_t)s->text[k]);
            if (n < 0x28) {
                uint8_t d = (uint8_t)s->text[n - 1];

                if (d != '.' && d != '!' && d != '?' && d != ',' && d != ';') {
                    Engine_InPut(self, ' ');
                    Engine_InPut(self, '.');
                }
            }
        } else if (self->fmt != 0) {
            Engine_InPut(self, '.');
            self->fmt = 0;
        }
        Engine_InPut(self, '\n');
    }

    p = self->feed_pos;
    if (p >= len) {
        Engine_InPutEnd(self);
        self->item_done = 1;
    }
    if (*ppos < p)
        self->st_input_empty = 0;
    *ppos = p;
}
