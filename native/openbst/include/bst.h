/* BeSTspeech: text in, samples out.
 *
 * One handle holds one build -- one language, one set of tables. Open it,
 * set what you want to change about the voice, and hand it text. The samples
 * come back signed sixteen bit, one channel, at the rate bst_rate gives.
 *
 * The tables the engine reads are compiled in, so nothing but this library
 * has to be present at run time. bst_open_image is there for a caller that
 * has an original binary and would rather read the tables out of it. */

#ifndef BST_H
#define BST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bst bst;

/* The builds this library carries, newest generation first. Fills names and
   returns how many there are; pass max 0 to count them. The strings are
   static and outlive any handle. */
int bst_builds(const char **names, int max);

/* Opens a build by name, on the tables compiled in. NULL if the name is not
   one of them, or if there is no memory. */
bst *bst_open(const char *build);

/* The same, on a caller's copy of an original binary. The bytes have to
   outlive the handle.

   The 1998 modules keep their excitation and gain tables in the core module
   all six languages share, so those builds need it as well and bst_open_image
   returns NULL for them rather than speaking from whatever is at the offset.
   Every other build ignores `core`. */
bst *bst_open_image(const char *build, const void *image, size_t len);
bst *bst_open_images(const char *build, const void *image, size_t len,
                     const void *core, size_t corelen);

void bst_close(bst *h);

/* The sample rate of this build, in hertz. */
int bst_rate(const bst *h);

/* Reads and writes one voice setting. The names are "pitch", the lowest
   period the contour reaches; "top", the highest; "level", how much the
   phrase rises and falls; "voice", which of the build's voices to speak in;
   and "rate", how fast. Two more, "exc" and "unvoiced", carry the excitation a
   phrase opens on and the unvoiced-frame gain adjustment, which the engine's
   own text escapes set and nothing else reaches. bst_set returns zero on an
   unknown name, bst_get returns zero for one. */
int bst_set(bst *h, const char *name, int value);
int bst_get(const bst *h, const char *name);

/* Says the text into pcm and returns how many samples it wrote, or -1 on a
   bad argument. Writes nothing past max; what will not fit is dropped, so a
   caller that cannot lose any should ask bst_length first. */
long bst_say(bst *h, const char *text, int16_t *pcm, long max);

/* How many samples the text will come to, without keeping any of them. */
long bst_length(bst *h, const char *text);

#ifdef __cplusplus
}
#endif

#endif
