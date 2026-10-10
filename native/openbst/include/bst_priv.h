/* What the tools reach into a handle for. Not part of the library's face. */

#ifndef BST_PRIV_H
#define BST_PRIV_H

#include "bst.h"
#include "bst_synth.h"
#include "bst_text.h"

const bst_image *bst_handle_image(const bst *h);
void             bst_handle_tables(const bst *h, bst_tables *out);

#endif
