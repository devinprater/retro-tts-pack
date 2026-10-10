/* zf1_dat.c - MSTTSLocEnUS.dat container reader (see notes/frontend.md 1.1).  Portable C99. */
#include "zf1_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void walk(zf1_dat *d, size_t o, size_t end, int depth)
{
    while (o + 0x28 <= end) {
        const uint8_t *h = d->buf + o;
        uint64_t sz = zf_rd64(h + 32);
        size_t c = o + 0x28;
        uint32_t t1 = zf_rd32(h);
        if (c + sz > end) return;
        if (t1 == 0x099f9814u || t1 == 0x3e13f66au || t1 == 0xe5f704bcu) {
            walk(d, c, c + (size_t)sz, depth + 1);
        } else if (depth >= 2 && d->nres < 64) {
            zf1_res *r = &d->res[d->nres++];
            memcpy(r->type, h, 16);
            memcpy(r->id, h + 16, 16);
            r->type1 = t1;
            r->id1 = zf_rd32(h + 16);
            r->p = d->buf + c;
            r->n = (size_t)sz;
        }
        o = (c + (size_t)sz + 7) & ~(size_t)7;
    }
}

int zf1_dat_load(zf1_dat *d, const char *path)
{
    FILE *f;
    long n;
    memset(d, 0, sizeof *d);
    if (!path) path = "C:\\Windows\\Speech_OneCore\\Engines\\TTS\\en-US\\MSTTSLocEnUS.dat";
    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0x28) { fclose(f); return -2; }
    d->buf = (uint8_t *)malloc((size_t)n + 16);
    if (!d->buf) { fclose(f); return -3; }
    if (fread(d->buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d->buf); d->buf = NULL; return -4; }
    fclose(f);
    memset(d->buf + n, 0, 16);
    d->size = (size_t)n;
    walk(d, 0, d->size, 0);
    return d->nres > 0 ? 0 : -5;
}

void zf1_dat_free(zf1_dat *d)
{
    free(d->buf);
    memset(d, 0, sizeof *d);
}

const uint8_t *zf1_dat_get(const zf1_dat *d, uint32_t type1, uint32_t id1, size_t *size)
{
    int i;
    for (i = 0; i < d->nres; i++)
        if (d->res[i].type1 == type1 && d->res[i].id1 == id1) {
            if (size) *size = d->res[i].n;
            return d->res[i].p;
        }
    if (size) *size = 0;
    return NULL;
}
