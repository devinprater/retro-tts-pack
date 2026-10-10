/* zf1_t_wb.c - word breaker test: reads UTF-8 lines "flag<TAB>text", prints tokens joined by '|' */
#include "zf1_wb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const zf_char *t; int first; } ctx_t;
static int emit(void *c, int off, int len)
{
    ctx_t *x = (ctx_t *)c; char *u = zf_to_utf8(x->t + off, len);
    printf("%s%s", x->first ? "" : "|", u); x->first = 0; free(u); return 0;
}
int main(int argc, char **argv)
{
    zf1_dat d; zf1_wb wb; char line[20000]; FILE *f = fopen(argv[1], "rb");
    if (zf1_dat_load(&d, NULL) || zf1_wb_init(&wb, &d, 0x409)) { printf("init failed\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        int n; size_t l = strlen(line); zf_char *w; ctx_t c;
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        w = zf_from_utf8(line + 2, -1, &n);
        c.t = w; c.first = 1;
        zf1_wb_break(&wb, w, n, line[0] - '0', emit, &c);
        printf("\n"); free(w);
    }
    return 0;
}
