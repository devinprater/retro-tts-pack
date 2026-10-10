/* zf1_t_dat.c - smoke test: list the resources of the .dat */
#include "zf1_int.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    zf1_dat d;
    int i;
    if (zf1_dat_load(&d, argc > 1 ? argv[1] : NULL)) { printf("load failed\n"); return 1; }
    for (i = 0; i < d.nres; i++)
        printf("%08x %08x off %08zx size %zx\n", d.res[i].type1, d.res[i].id1, (size_t)(d.res[i].p - d.buf), d.res[i].n);
    zf1_dat_free(&d);
    return 0;
}
