/* Written by tools/lift.sh. The index the library looks a build up
   in; the tables themselves are one file to a build beside this. */

#include <string.h>
#include "bst_text.h"

extern const bst_lifted BST_DATA_1995;
extern const bst_lifted BST_DATA_1998ENG;
extern const bst_lifted BST_DATA_1998DUT;
extern const bst_lifted BST_DATA_1998FRN;
extern const bst_lifted BST_DATA_1998GRM;
extern const bst_lifted BST_DATA_1998ITL;
extern const bst_lifted BST_DATA_1998SPN;
extern const bst_lifted BST_DATA_2006ARA;
extern const bst_lifted BST_DATA_2006DUT;
extern const bst_lifted BST_DATA_2006ENG;
extern const bst_lifted BST_DATA_2006FRE;
extern const bst_lifted BST_DATA_2006GER;
extern const bst_lifted BST_DATA_2006GRE;
extern const bst_lifted BST_DATA_2006HEB;
extern const bst_lifted BST_DATA_2006ITA;
extern const bst_lifted BST_DATA_2006JPN;
extern const bst_lifted BST_DATA_2006POL;
extern const bst_lifted BST_DATA_2006POR;
extern const bst_lifted BST_DATA_2006RUS;
extern const bst_lifted BST_DATA_2006SPA;

static const struct { const char *name; const bst_lifted *d; } INDEX[] = {
    { "1995", &BST_DATA_1995 },
    { "1998ENG", &BST_DATA_1998ENG },
    { "1998DUT", &BST_DATA_1998DUT },
    { "1998FRN", &BST_DATA_1998FRN },
    { "1998GRM", &BST_DATA_1998GRM },
    { "1998ITL", &BST_DATA_1998ITL },
    { "1998SPN", &BST_DATA_1998SPN },
    { "2006ARA", &BST_DATA_2006ARA },
    { "2006DUT", &BST_DATA_2006DUT },
    { "2006ENG", &BST_DATA_2006ENG },
    { "2006FRE", &BST_DATA_2006FRE },
    { "2006GER", &BST_DATA_2006GER },
    { "2006GRE", &BST_DATA_2006GRE },
    { "2006HEB", &BST_DATA_2006HEB },
    { "2006ITA", &BST_DATA_2006ITA },
    { "2006JPN", &BST_DATA_2006JPN },
    { "2006POL", &BST_DATA_2006POL },
    { "2006POR", &BST_DATA_2006POR },
    { "2006RUS", &BST_DATA_2006RUS },
    { "2006SPA", &BST_DATA_2006SPA },
};

const bst_lifted *bst_lifted_for(const char *build) {
    if (!build) return NULL;
    for (size_t i = 0; i < sizeof INDEX / sizeof INDEX[0]; i++)
        if (strcmp(INDEX[i].name, build) == 0) return INDEX[i].d;
    return NULL;
}
