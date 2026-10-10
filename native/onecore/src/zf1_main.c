/* zf1_main.c - zf1 test driver: corpus -> dump in the harness/zftap1.exe format (L/S/W lines), for
 * tests/fe1/compare.py.
 * Usage: zf1_main <corpus.txt> <out.dump> [MSTTSLocEnUS.dat]
 * Each non-empty corpus line not starting with "##" is one Speak call (SAPI XML if it starts with '<'). */
#include "zf.h"
#include "zf1_frag.h"
const zf1_frags *zf1_debug_frags(const zf1_engine *e);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;

static void putw8(const zf_char *s, int n)
{
    int i;
    if (!s) { fputs("(null)", out); return; }
    for (i = 0; n < 0 ? s[i] != 0 : i < n; i++) {
        unsigned c = s[i];
        char b[8];
        int k = 0;
        if (c >= 0xD800 && c < 0xDC00 && (n < 0 ? s[i + 1] != 0 : i + 1 < n)) {
            unsigned d = s[i + 1];
            if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; }
        }
        if (c < 0x20 || c == '\\' || c == '"' || c == 0x7f) { fprintf(out, "\\x%02x", c); continue; }
        if (c < 0x80) b[k++] = (char)c;
        else if (c < 0x800) { b[k++] = (char)(0xC0 | (c >> 6)); b[k++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { b[k++] = (char)(0xE0 | (c >> 12)); b[k++] = (char)(0x80 | ((c >> 6) & 63)); b[k++] = (char)(0x80 | (c & 63)); }
        else { b[k++] = (char)(0xF0 | (c >> 18)); b[k++] = (char)(0x80 | ((c >> 12) & 63)); b[k++] = (char)(0x80 | ((c >> 6) & 63)); b[k++] = (char)(0x80 | (c & 63)); }
        fwrite(b, 1, (size_t)k, out);
    }
}

static void pstr(const char *name, const zf_char *s, int always)
{
    if (!always && (!s || !s[0])) return;
    fprintf(out, " %s=\"", name);
    if (s) putw8(s, -1);
    fputc('"', out);
}

static void dump_word(const zf_word *w, const zf_sentence *s, int *grpmap, int *ngrp)
{
    int k, g = 0;
    fprintf(out, "W");
    pstr("text", w->text, 1);
    fprintf(out, " u80=%u pos=%u tpos=%u", w->lang, w->pos, w->tpos);
    fprintf(out, " i84=%d i88=%d i180=%d i210=%d i214=%d i228=%d i22c=%d i230=%d i234=%d i238=%d i23c=%d i240=%d"
                 " i244=%d i248=%d i24c=%d i250=%d i254=%d i258=%d i270=%d i274=%d i278=%d i27c=%d i2b8=%d i2bc=%d"
                 " i2f0=%d i368=%d i36c=%d",
            w->i84, w->type, w->i180, w->silence_ms, w->i214, w->i228, w->i22c, w->i230, w->i234, w->tone,
            w->tone_lock, w->i240, w->bi, w->i248, w->bi_lock, w->pause_class, w->emphasis, w->emph_lock, w->src_off,
            w->src_len, w->i278, w->i27c, w->i2b8, w->i2bc, w->i2f0, w->i368, w->i36c);
    pstr("sb8", w->regular, 0);
    pstr("sd8", w->ci, 0);
    pstr("sf8", w->prev_chars, 0);
    pstr("s118", w->next_chars, 0);
    pstr("s160", w->pron, 0);
    pstr("s188", w->ne_type, 0);
    pstr("s1b0", w->s1b0, 0);
    pstr("s1d0", w->s1d0, 0);
    pstr("s1f0", w->s1f0, 0);
    pstr("s2f8", w->s2f8, 0);
    if (w->nprons > 0) {
        fprintf(out, " prons=[");
        for (k = 0; k < w->nprons; k++) {
            if (k) fputc('|', out);
            if (k == w->cur_pron) fputc('*', out);
            /* the engine's CTTSString keeps a NULL buffer for an empty string: the harness prints "(null)" */
            putw8(w->prons[k] && w->prons[k][0] ? w->prons[k] : NULL, -1);
        }
        fputc(']', out);
    }
    fprintf(out, " fs=%d,%x,%d,%d,%x", w->fs.action, w->fs.lang, w->fs.emph, w->fs.silence_ms, w->fs.pos);
    if (w->fs.has_prosody) fprintf(out, " pro=%g,%g,%g", w->fs.pitch_middle, w->fs.rate, w->fs.volume);
    if (w->fs.sayas) { fprintf(out, " sayas=\""); putw8(w->fs.sayas, -1); fputc('"', out); }
    fprintf(out, " fso=%d,%d", w->fs.src_off, w->fs.src_len);
    if (w->group) {
        for (k = 0; k < *ngrp; k++) if (grpmap[k] == w->group) break;
        if (k == *ngrp && *ngrp < 4096) grpmap[(*ngrp)++] = w->group;
        g = k + 1;
    }
    fprintf(out, " grp=%d ap=%d ne=%d\n", g, w->attached_pause, w->ne_obj);
    (void)s;
}

int main(int argc, char **argv)
{
    FILE *cf;
    char *buf, *p;
    long n;
    int lineno = 0;
    zf1_engine *e;
    static int grpmap[4096];
    if (argc < 3) { fprintf(stderr, "usage: zf1_main corpus.txt out.dump [dat]\n"); return 1; }
    e = zf1_open(argc > 3 ? argv[3] : NULL);
    if (!e) { fprintf(stderr, "zf1_open failed\n"); return 1; }
    cf = fopen(argv[1], "rb");
    if (!cf) { fprintf(stderr, "cannot open corpus\n"); return 1; }
    fseek(cf, 0, SEEK_END);
    n = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, cf) != (size_t)n) { fclose(cf); return 1; }
    buf[n] = 0;
    fclose(cf);
    out = fopen(argv[2], "wb");
    if (!out) return 1;
    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        char *eol = p;
        int len;
        while (*eol && *eol != '\n') eol++;
        len = (int)(eol - p);
        if (len && p[len - 1] == '\r') len--;
        lineno++;
        if (len > 0 && !(len >= 2 && p[0] == '#' && p[1] == '#')) {
            int wl;
            zf_char *w = zf_from_utf8(p, len, &wl);
            zf_sentence s;
            fprintf(out, "L %d\t", lineno);
            putw8(w, wl);
            fputc('\n', out);
            if (zf1_speak(e, w, wl, -1) == 0) {
                const zf1_frags *fr = zf1_debug_frags(e);
                int f;
                for (f = 0; f < fr->nsf; f++) {
                    const zf1_sfrag *sf = &fr->sf[f];
                    const uint16_t *ph;
                    fprintf(out, "F act=%d emph=%d rate=%d vol=%u pitch=%d/%d sil=%u pos=%u cat=", sf->action, sf->emph,
                            sf->rate, (unsigned)sf->vol, sf->pitch, sf->range, (unsigned)sf->sil, (unsigned)sf->pos);
                    if (sf->ctx) putw8(sf->ctx, -1); else fputc('-', out);
                    fprintf(out, " before=- after=- ph=");
                    for (ph = sf->phones; ph && *ph; ph++) fprintf(out, "%s%u", ph == sf->phones ? "" : ",", *ph);
                    fprintf(out, " off=%u len=%u \"", (unsigned)sf->ofs, (unsigned)sf->len);
                    if (sf->text) putw8(sf->text, sf->len);
                    fprintf(out, "\"\n");
                }
                while (zf1_next_sentence(e, &s) == 1) {
                    int i, ngrp = 0;
                    fprintf(out, "S off=%d len=%d type=%d emo=%d\n", s.src_off, s.src_len, s.type, s.emotion);
                    for (i = 0; i < s.nwords; i++) dump_word(&s.words[i], &s, grpmap, &ngrp);
                    zf_sentence_free(&s);
                }
            }
            free(w);
        }
        p = *eol ? eol + 1 : eol;
    }
    fclose(out);
    zf1_close(e);
    free(buf);
    return 0;
}
