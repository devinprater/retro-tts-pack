/* zf1_t_ss.c - sentence separator test driver.  Same input/output as harness/zftap1_ss.exe "direct" mode:
 *   zf1_t_ss <lines.txt> <out.txt>
 * Each input line (UTF-8, escapes \n \t \r \\ \uXXXX, "##" = comment) is split like the enumerator does it
 * within one fragment: call zf1_ss_run on the rest of the line until it is used up; print
 *   L <lineno>\t<text>      and per call     SD <pos> <consumed> <hard_end>   */
#include "zf1_ss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void putu8(FILE *f, const zf_char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x20 || c == '\\' || c == '"' || c == 0x7f) { fprintf(f, "\\x%02x", c); continue; }
        if (c < 0x80) fputc((int)c, f);
        else if (c < 0x800) { fputc(0xC0 | (c >> 6), f); fputc(0x80 | (c & 63), f); }
        else if (c < 0x10000) { fputc(0xE0 | (c >> 12), f); fputc(0x80 | ((c >> 6) & 63), f); fputc(0x80 | (c & 63), f); }
        else { fputc(0xF0 | (c >> 18), f); fputc(0x80 | ((c >> 12) & 63), f); fputc(0x80 | ((c >> 6) & 63), f); fputc(0x80 | (c & 63), f); }
    }
}

int main(int argc, char **argv)
{
    zf1_dat d;
    zf1_ss ss;
    FILE *in, *out;
    static char line[1 << 16];
    int lineno = 0;
    if (argc < 3) { fprintf(stderr, "usage: zf1_t_ss lines.txt out.txt\n"); return 1; }
    if (zf1_dat_load(&d, NULL) || zf1_ss_init(&ss, &d)) { fprintf(stderr, "init failed\n"); return 1; }
    in = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (!in || !out) return 1;
    if (argc > 3 && !strcmp(argv[3], "enum")) {
        /* enumerator mode: blocks "L <n>" / "F <action> <nextchar> <sayas|-> <text with \\uXXXX escapes>" / "E";
         * prints "SN s=f:p e=f:p" per sentence and "SN r=1" at the end (same as harness zftap1_ss.exe) */
        zf1_ssfrag fr[256];
        zf_char *buf[256], *sa[256];
        int nf = 0;
        while (fgets(line, sizeof line, in)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            if (line[0] == 'L') { fprintf(out, "%s\n", line); nf = 0; continue; }
            if (line[0] == 'F' && nf < 256) {
                int act = 0, n, k = 0, j;
                unsigned nx = 0;
                char said[256];
                char *p = line + 2, *q;
                zf_char *w;
                act = (int)strtol(p, &p, 10);
                nx = (unsigned)strtoul(p, &p, 10);
                p++;
                q = strchr(p, ' ');
                if (!q) q = p + strlen(p);
                memcpy(said, p, (size_t)(q - p));
                said[q - p] = 0;
                p = *q ? q + 1 : q;
                w = zf_from_utf8(p, -1, &n);
                for (j = 0; j < n; j++) {
                    if (w[j] == '\\' && j + 1 < n) {
                        j++;
                        if (w[j] == 'u' && j + 4 < n) {
                            char hx[5];
                            int t;
                            for (t = 0; t < 4; t++) hx[t] = (char)w[j + 1 + t];
                            hx[4] = 0;
                            w[k++] = (zf_char)strtoul(hx, NULL, 16);
                            j += 4;
                        } else w[k++] = w[j];
                    } else w[k++] = w[j];
                }
                w[k] = (zf_char)nx;
                buf[nf] = w;
                sa[nf] = strcmp(said, "-") ? zf_from_utf8(said, -1, NULL) : NULL;
                fr[nf].action = act;
                fr[nf].text = w;
                fr[nf].len = k;
                fr[nf].sayas = sa[nf];
                nf++;
                continue;
            }
            if (line[0] == 'E') {
                zf1_sspos cur = {0, 0}, a, b;
                int r, i;
                while ((r = zf1_ss_next(&ss, fr, nf, &cur, &a, &b)) == 1)
                    fprintf(out, "SN s=%d:%d e=%d:%d\n", a.frag, a.frag < 0 ? -1 : a.pos, b.frag, b.frag < 0 ? -1 : b.pos);
                fprintf(out, "SN r=1\n");
                for (i = 0; i < nf; i++) { free(buf[i]); free(sa[i]); }
                nf = 0;
            }
        }
        fclose(out);
        return 0;
    }
    while (fgets(line, sizeof line, in)) {
        size_t l = strlen(line);
        int n, k = 0, j, pos = 0;
        zf_char *w;
        char *p = line;
        lineno++;
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (lineno == 1 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB) p += 3;
        if (!*p || (p[0] == '#' && p[1] == '#')) continue;
        w = zf_from_utf8(p, -1, &n);
        for (j = 0; j < n; j++) {
            if (w[j] == '\\' && j + 1 < n) {
                j++;
                if (w[j] == 'n') w[k++] = 10;
                else if (w[j] == 't') w[k++] = 9;
                else if (w[j] == 'r') w[k++] = 13;
                else if (w[j] == 'u' && j + 4 < n) {
                    char hx[5];
                    int q;
                    for (q = 0; q < 4; q++) hx[q] = (char)w[j + 1 + q];
                    hx[4] = 0;
                    w[k++] = (zf_char)strtoul(hx, NULL, 16);
                    j += 4;
                } else w[k++] = w[j];
            } else w[k++] = w[j];
        }
        w[k] = 0;
        if (argc > 3) {   /* replay mode: line = "<next char code>\t<text>" -> one call, text[len] = next */
            int t = 0, c, h;
            zf_char nxt = 0;
            while (t < k && w[t] != '\t') nxt = (zf_char)(nxt * 10 + (w[t++] - '0'));
            t++;
            w[k] = nxt;
            zf1_ss_run(&ss, w + t, k - t, &c, &h);
            fprintf(out, "SS %d %d\n", c, h);
            free(w);
            continue;
        }
        fprintf(out, "L %d\t", lineno);
        putu8(out, w, k);
        fputc('\n', out);
        while (pos < k) {
            int c, h;
            zf1_ss_run(&ss, w + pos, k - pos, &c, &h);
            fprintf(out, "SD %d %d %d\n", pos, c, h);
            if (c <= 0) break;
            pos += c;
        }
        free(w);
    }
    fclose(out);
    zf1_ss_free(&ss);
    zf1_dat_free(&d);
    return 0;
}
