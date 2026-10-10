#include "zf1_pron.h"
#include <stdio.h>
int main(void)
{
    zf1_dat d; zf1_lex lx; zf1_phoneset ps; zf1_pron p; size_t n; const uint8_t *r;
    static const uint16_t a[] = {41,35,8,32,10,9,41,35,0}, b[] = {26,21,8,31,35,0}, c[] = {46,22,8,31,19,0};
    const uint16_t *t[3] = {a, b, c};
    zf1_sapiseg seg[8]; int i, k, j;
    zf1_dat_load(&d, NULL);
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n); zf1_lex_init(&lx, r, n);
    r = zf1_dat_get(&d, 0x29a5584b, 0x153f1b64, &n); zf1_phoneset_init(&ps, r, n);
    zf1_pron_init(&p, &d, &lx, &ps);
    for (i = 0; i < 3; i++) {
        int ns = zf1_pron_sapi(&p, t[i], seg, 8);
        for (j = 0; j < ns; j++) { for (k = 0; seg[j].ph[k]; k++) printf("%x.", seg[j].ph[k]); printf(" c%d | ", seg[j].comma_after); }
        printf("\n");
    }
    return 0;
}
