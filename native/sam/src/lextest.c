/* lextest: print lexicon / LTS / full-chain pronunciations for comparison with the real engine.
 *   lextest lex   file.lxa  words.txt     lexicon only (lexdump format)
 *   lextest lts   file.lxa  words.txt     letter-to-sound only (lexdump format)
 *   lextest chain lex.lxa lts.lxa words.txt   lexicon -> morphology -> LTS: word, lextype, pron, POS list
 */
#include "sam_lex.h"

#include <stdio.h>
#include <string.h>

static void chomp(char *line)
{
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
}

int main(int argc, char **argv)
{
    char line[512];
    sam_pron pr[16];
    FILE *f;
    sam_lexicon *lex = NULL;
    sam_lts *lts = NULL;
    if (argc < 4) return 1;
    if (!strcmp(argv[1], "chain")) {
        sam_lexicon *x;
        sam_lts *m;
        if (argc < 5) return 1;
        x = sam_lexicon_load(argv[2]);
        m = sam_lts_load(argv[3]);
        f = fopen(argv[4], "rb");
        if (!x || !m || !f) return 1;
        while (fgets(line, sizeof line, f)) {
            sam_lookup r;
            int k;
            chomp(line);
            if (!line[0] || !sam_word_lookup(x, m, line, &r)) continue;
            printf("%s\t%x\t", line, (unsigned)r.lextype);
            for (k = 0; k < r.n; k++) printf("%u ", r.pron[k]);
            printf("\t");
            for (k = 0; k < r.nposa; k++) printf("%x ", (unsigned)r.posa[k]);
            printf("\n");
        }
        fclose(f);
        return 0;
    }
    if (!strcmp(argv[1], "lex")) lex = sam_lexicon_load(argv[2]);
    else lts = sam_lts_load(argv[2]);
    if (!lex && !lts) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    f = fopen(argv[3], "rb");
    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        int n, i, k;
        chomp(line);
        if (!line[0]) continue;
        n = lex ? sam_lexicon_lookup(lex, line, pr, 16) : sam_lts_pronounce(lts, line, pr, 16);
        if (n == 0) printf("%s\t-\t-\t(none)\n", line);
        for (i = 0; i < n; i++) {
            printf("%s\t15\t%x\t", line, (unsigned)pr[i].pos);
            for (k = 0; k < pr[i].n; k++) printf("%u ", pr[i].ph[k]);
            printf("\n");
        }
    }
    fclose(f);
    return 0;
}
