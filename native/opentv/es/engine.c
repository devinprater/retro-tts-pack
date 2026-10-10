/*
 * The engine object's life: construction, the defaults it starts from, the
 * reset that puts it back there, and the step function the host calls until
 * there is nothing left to say.
 *
 * Engine_Step is the whole pipeline seen from above.  Each stage runs only
 * when the one below it has work and there are enough free nodes to hold the
 * result, so the five stages advance in lockstep without threads and without
 * blocking, inside a loop the caller drives.
 */
#include "es_engine.h"

/* @0x100613c8 */
extern const uint8_t g_default_params[22];

/* Zero the track block and set the handful of defaults the host reads back.
 * The memset covers 0x19ac bytes from the track block onwards -- the same
 * 0x19ac the English constructor clears from offset 0, that block having
 * moved from the front of the object to the back. */
/* @0x1000ddc0 */
Engine *TV_THISCALL Engine_Construct(Engine *self)
{
    memset(&self->synth_busy, 0, 0x19ac);
    self->sample_rate = 8000;
    self->sapi = NULL;
    self->item_notify = 0;
    self->item_done = 1;
    self->cur_voice = 0;
    self->cur_bac = 0;
    self->mode_I_on = 0;
    self->fmt = 1;
    self->cur_pitch = 85;
    self->cur_speed = 150;
    self->cur_volume = 0xffff;
    return self;
}

/* Default voice parameters, then a full reset.  The original tail-jumps into
 * Engine_Reset rather than calling it. */
/* @0x100086a0 */
int32_t TV_THISCALL Engine_Init(Engine *self)
{
    int i;

    self->mode_P = 1;
    self->rate_index = 13;
    self->speed_wpm = 150;
    self->pitch = 85;
    self->flags_N = 0x1780;
    self->flags_A = 0x40;
    self->mode_I = 0;
    self->volume_atten = 0;
    self->rate_class = 0;
    self->voice = 0;
    for (i = 0; i < 22; i++)
        self->cfg_bytes[i] = g_default_params[i];
    self->stop_mark = 0;
    return Engine_Reset(self);
}

/* Clear all pipeline state.  Returns reset_value when a reset was asked for
 * with a stop mark pending, else -1. */
/* @0x10008700 */
int32_t TV_THISCALL Engine_Reset(Engine *self)
{
    int32_t ret = -1;

    if (self->stop_mark)
        ret = self->reset_value;
    self->st_2110 = 0;
    self->st_input_empty = 1;
    self->st_idle = 1;
    self->st_2113 = 0;
    self->st_2114 = 0;
    self->st_2115 = 0;
    self->st_2116 = 0;
    self->st_2117 = 0;
    self->skip_text = 0;
    self->reset_pending = 0;
    self->stop_mark = 0;
    self->out_state = 2;
    Preformat_Reset(self);
    Engine_ResetNodes(self);
    Synth_ResetTracks(self);
    Synth_InitFilters(self);
    Engine_ResetRings(self);
    Output_Reset(self, (int16_t)self->sample_rate);
    Stage4_Reset(self);
    Prosody_Reset(self);
    Stage3_Reset(self);
    Stage2_Reset(self);
    Stage1_Reset(self);
    Stage0_Reset(self);
    return ret;
}

/* @0x1000e240 */
void TV_THISCALL Engine_ResetRings(Engine *self)
{
    self->in_wr = 0;
    self->in_rd = 0;
    self->pre_wr = 0;
    self->pre_rd = 0;
    self->mid_wr = 0;
    self->mid_rd = 0;
    self->e_9180 = -1;
    self->in_flags_N = 0x1780;
    self->in_flags_A = 0x40;
}

/* Thread the whole pool onto the free list between its two sentinels, empty
 * the work list, and hand the current voice parameters to all five stages. */
/* @0x10008ef0 */
void TV_THISCALL Engine_ResetNodes(Engine *self)
{
    Node *prev, *n;
    int i;

    self->stage = NULL;
    self->free_nodes = TV_ES_NODE_POOL;
    self->free_head = &self->free_head_node;
    self->free_head->prev = NULL;
    self->free_head->flags |= NODE_SENTINEL;
    self->free_tail = &self->free_tail_node;
    self->free_tail->next = NULL;
    self->free_tail->flags |= NODE_SENTINEL;

    prev = self->free_head;
    for (i = 0; i < TV_ES_NODE_POOL; i++) {
        n = &self->node_pool[i];
        prev->next = n;
        n->prev = prev;
        n->flags = (n->flags & ~1u) | NODE_FREE;
        prev = n;
    }
    self->free_tail->prev = &self->node_pool[TV_ES_NODE_POOL - 1];

    self->work_tail = &self->work_tail_node;
    self->work_head = &self->work_head_node;
    self->work_tail->next = NULL;
    self->work_tail->prev = self->work_head;
    self->work_tail->flags |= NODE_SENTINEL;
    self->work_head->next = self->work_tail;
    self->work_head->prev = NULL;
    self->work_head->flags |= NODE_SENTINEL;

    for (i = 0; i < 5; i++) {
        StageCtx *s = &self->stage_ctx[i];
        s->first = NULL;
        s->cur = NULL;
        s->ctl = NULL;
        s->scan = NULL;
        s->last = NULL;
        s->d14 = NULL;
        s->d18 = NULL;
        s->p_1c = self->mode_I;
        s->p_20 = self->mode_P;
        s->rate_index = self->rate_index;
        s->pitch = self->pitch;
        s->volume_atten = self->volume_atten;
        s->p_30 = self->rate_class;
        s->p_34 = self->flags_N;
        /* p_38 is not written.  English copies flags_A into it here; this
         * engine leaves whatever the escape parser last put there, so a
         * reset does not undo an ESC[A or ESC[D. */
        s->voice = self->voice;
    }
}

/* One turn of the pipeline.  Returns 2 when PCM is waiting in out_buf, 0
 * when nothing came out, and adds 1 to either when the engine has run dry. */
/* @0x100087d0 */
int32_t TV_THISCALL Engine_Step(Engine *self)
{
    uint8_t produced = 0;

    if (self->reset_pending)
        Engine_Reset(self);
    if (self->out_state != 2)
        self->out_state = Stage4_Run(self);

    if (self->st_2117) {
        if (Tracks_Op(self, 1, self->trk_38))
            self->st_2117 = 0;
    }
    if (!self->st_2117 && self->st_2116) {
        switch (Stage3_Run(self)) {
        case 0:
            self->st_2116 = 0;
            break;
        case 1:
            self->st_2117 = 1;
            break;
        }
        for (;;) {
            if (self->synth_busy || self->synth_hold)
                break;
            if (!Synth_Step(self))
                break;
            if (!self->synth_19ad)
                break;
            if (self->trk_34 != 0) {
                produced = 1;
                self->out_state = Stage4_Run(self);
                if (self->out_count > 12000)
                    break;
            } else if (self->out_count > 12000) {
                produced = 1;
                break;
            }
        }
        self->out_state = Stage4_Run(self);
    } else if (self->st_2115) {
        if (self->free_nodes > 0x69) {
            if (Stage2_Run(self))
                self->st_2116 = 1;
            else
                self->st_2115 = 0;
        }
    } else if (self->st_2114) {
        if (self->free_nodes > 0x37) {
            if (Stage1_Run(self))
                self->st_2115 = 1;
            else
                self->st_2114 = 0;
        }
    } else if (self->st_2113) {
        if (self->free_nodes > 0x37) {
            if (Stage0_Run(self))
                self->st_2114 = 1;
            else
                self->st_2113 = 0;
        }
    } else if (self->st_idle) {
        return (self->out_count != 0 ? 2 : 0) + 1;
    } else if (self->free_nodes > 0xf) {
        if (Engine_InputStage(self))
            self->st_2113 = 1;
        else
            self->st_idle = 1;
    }
    return produced ? 2 : 0;
}
