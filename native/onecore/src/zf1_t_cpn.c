/* zf1_t_cpn.c - probe: COMPOUNDPRON network (19a6569a) parses via fork B's runtime */
#include "zf1_fst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    zf1_dat d; size_t n; const uint8_t *r; zf1_net *net; int i, mode;
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x19a6569a, 0xea8d0701, &n);
    net = zf1_net_load(r, n);
    if (!net) { printf("load failed\n"); return 1; }
    zf1_net_set_mask(net, 1);
    for (i = 1; i < argc; i++) {
        int wl; zf_char *w = zf_from_utf8(argv[i], -1, &wl);
        for (mode = 0; mode <= 2; mode++) {
            zf1_netres res; int k;
            memset(&res, 0, sizeof res);
            int rc = zf1_net_parse(net, w, wl, 0, mode, NULL, 0, &res);
            printf("%s mode%d rc=%d found=%d len=%d leaves:", argv[i], mode, rc, res.found, res.len);
            for (k = 0; k < res.nleaves; k++) {
                char *u = zf_to_utf8(res.leaves[k].text, res.leaves[k].len);
                printf(" [%d-%d g%d '%s']", res.leaves[k].start, res.leaves[k].end, res.leaves[k].glue, u);
                free(u);
            }
            printf("\n");
            zf1_netres_free(&res);
        }
    }
    return 0;
}
