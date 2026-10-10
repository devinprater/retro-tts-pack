#ifndef BST_TEXT_H
#define BST_TEXT_H

#include <stdint.h>
#include <stddef.h>

/* The word front end: normalisation, dictionary lookup and letter-to-sound.
   Everything here reads its tables out of a loaded BeSTspeech image rather
   than embedding them, so the library carries no lifted data of its own. */

#define BST_SECTIONS 20

/* ---- the table directory ------------------------------------------------

   Every address the library reads out of an image is named here rather than
   written into the code, because the same engine ships in three generations
   and nineteen languages and only the addresses move. A build is described by
   one bst_tabmap; the code never mentions a number.

   Addresses are whatever the image's own addressing is: a virtual address in
   a PE build, a segment and offset packed as (segment << 16) | offset in a
   16-bit NE one. bst_at resolves either. */

enum {
    BST_H_LETTER, BST_H_DIGIT, BST_H_EAT1, BST_H_SPACE, BST_H_DOT,
    BST_H_CURRENCY, BST_H_PUNCT, BST_H_DASH, BST_H_EXPONENT, BST_H_MODE1,
    BST_H_MODE2, BST_H_DEL, BST_H_OPENER, BST_H_TILDE, BST_H_APOSDOT,
    BST_H_APOS, BST_H_POSSESS, BST_H_EAT2, BST_H_EAT3, BST_H_WORD,
    BST_H_NUMBER, BST_H_DOTTED, BST_H_SEP, BST_H_GROUPS, BST_H_SEPNUM,
    BST_H_MONEY, BST_H_DASH2, BST_H_ORDINAL, BST_H_ORDEMIT, BST_H_PUNCTOUT,
    BST_H_DOTOUT, BST_H_ONEDIGIT, BST_H_DASHLETTER,
    BST_H_HIGH, BST_H_HIGH2, BST_H_HIGHOUT,
    BST_H_COUNT
};

enum {
    BST_S_POINT, BST_S_DOLLARS, BST_S_DOLLAR, BST_S_AND, BST_S_CENTS,
    BST_S_ORD_ST, BST_S_ORD_ND, BST_S_ORD_RD, BST_S_ORD_TIETH,
    BST_S_ORD_FIFTH, BST_S_ORD_FIRST, BST_S_ORD_TH, BST_S_GRPSEP,
    BST_S_PLURAL, BST_S_DIGITS, BST_S_TENS, BST_S_TEENS, BST_S_SCALES,
    BST_S_OH, BST_S_HUNDRED, BST_S_ZERO, BST_S_ONE_ALT, BST_S_HUNDREDS,
    BST_S_SCALE_PL, BST_S_ONE_BIG,
    BST_S_COUNT
};

#define BST_BUCKETS 15

typedef struct {
    /* character classification */
    uint32_t chattr, letterattr, casemap, symmap;
    /* phoneme attributes and the duration tables the frame builder reads */
    uint32_t phattr1, phattr2, classtab, exctab, basedur, coefgain;
    /* the tokeniser. code_lo and code_hi bound the executable section, which
       is how a transition row is told from the bytes after the last one: a row
       names its handler by address. The row itself is three columns whose
       widths and places differ between the 32-bit builds and the 16-bit one,
       which packs the two state columns into bytes and the handler into a near
       pointer, with a link to the next row between them. */
    uint32_t tokstates, names, code_lo, code_hi;
    uint8_t  tok_stride;
    uint8_t  tok_state_off, tok_state_w;
    uint8_t  tok_handler_off, tok_handler_w;
    uint8_t  tok_next_off, tok_next_w;

    /* The other records whose packing differs between generations. The 16-bit
       build squeezes a rule's priority into a byte and drops a byte of padding
       from a letter-to-sound index entry; its trie descriptor holds far
       pointers where the 32-bit one holds flat ones, which read the same way
       but sit at different offsets. */
    uint8_t  lts_index_stride;
    uint8_t  rule_stride, rule_prio_w;
    uint8_t  trie_st_off, trie_li_off, trie_base_off, trie_max_off, trie_po_off;
    /* How wide a trie state is. Four in the 1995 and 2006 builds, three in
       the 1998 ones. */
    uint8_t  trie_ent;

    /* Three constants the frame builder holds rather than reads. A silent
       frame's first byte, and how an unvoiced frame is chunked: the 1995 build
       holds it for one period of a hundred and ten samples, the 1998 one for
       eight periods of an eighth of that. The frame lasts as long either way,
       so the interpolation clock does not change; only the bytes do. */
    uint8_t  silence_f0, unvoiced_dur, unvoiced_reps;

    /* The shortest period a frame may carry, which is also the period a
       silent frame is given. Sixty-six everywhere but the 2006 builds. */
    uint8_t  min_period;

    /* Which coefficients round the interpolation's log index up rather than
       down, so a step over an odd distance is one larger. The 2006 builds do
       it for the first three; the earlier ones for none. */
    uint16_t interp_round_mask;

    /* Which coefficients the frame rounds away from zero on its way from the
       smoothed state to the byte a frame carries. The 2006 builds do it for
       the second and third; every other build truncates all ten. */
    uint16_t coef_round_mask;

    /* Where a segment record keeps its excitation class. The 1995 and 1998
       builds put the target index in the low nine bits and the class above
       it; the 2006 builds leave four bits between them and shift by thirteen.
       The first byte of a long silent stretch, and whether the excitation
       nibble has the two extra cases the earlier builds carry, go the same
       way: they are the shape of a frame, not a table. */
    uint8_t  class_shift;
    uint8_t  long_silence_chunk; /* a long silence is held in whole periods */
    /* An unvoiced frame carries no pitch fraction at all, not even the
       coefficient parity the others fold into that byte. */
    uint8_t  unvoiced_no_frac;
    /* The level a word takes when its every stress mark has been used, as a
       height above the middle. Five unless a build holds it lower, which the
       1998 French module does at four. It is not a ceiling on every code: the
       level a phrase reaches is higher than this and is not cut down to it. */
    uint8_t  level_max;
    uint8_t  long_silence_f0;
    uint8_t  exc_two_way;

    /* The 2006 builds compose an unvoiced frame as one long period and split
       it as it leaves: the repeat count goes in the first byte's low nibble,
       the period is divided by it, and the fractional part the second byte
       carries is dropped. */
    uint8_t  unvoiced_chunk;

    /* The 1995 and 1998 builds put a break into a sentence longer than one
       breath, at the last word whose opening marker allows one. The 2006
       builds dropped the rule and say a sentence in one phrase however long
       it is. */
    uint8_t  no_breath_break;

    /* The text has a closing brace appended to it, which shuts the last
       phrase and opens one more holding nothing but a pause. The earlier
       builds speak that phrase; the 2006 ones stop instead, and what reaches
       the buffer in its place is the twenty silent samples of their own
       shutdown. Two: the Japanese build drops it whatever the phrase before
       it ended on, where the others keep one a mark other than a stop
       leaves open. */
    uint8_t  no_closing_phrase;

    /* The 2006 builds want the first group of a number written with
       separators to be three digits like the rest. Given fewer they say that
       group and abandon the remainder of the text, which is what they do and
       so what this does. */
    uint8_t  sepnum_first_three;

    /* How long a pause the closing brace appended to the text leaves. One in
       the earlier builds; the 2006 ones treat it as a full stop and leave
       seven. Zero means one. */
    uint8_t  close_pause;

    /* A closing bracket written in the text. The earlier builds read it as
       the closing brace, which is the full stop that pause belongs to; the
       2006 ones read it as a comma instead, and go on rather than stopping
       the way their own comma does. */
    uint8_t  close_paren_comma;

    /* A lone letter is spelled, unless it is a word in its own right. Which
       letters those are is written into the reader rather than held in a
       table: the English builds name a, A and I, and no other build names
       any. Set means this build names none. */
    uint8_t  lone_no_words;

    /* The closing brace is the mark the reader adds after the text, and the
       2006 builds treat one written in the text as that same mark: the text
       stops there and nothing after it is read. The 1995 and 1998 builds,
       whose own tail is that character, read it as ordinary punctuation. */
    uint8_t  brace_ends_text;

    /* The Arabic build says nothing to a mark written in the text and does
       not break the phrase at one either: the whole text is one sentence,
       ended by the stop the reader adds after it. The comma is the exception,
       and ends the text the way it does in the other builds. */
    uint8_t  punct_dropped;

    /* Speak now ends the phrase, and in the 2006 builds it stops there: the
       break goes into the stream and the words after it are appended behind
       it, so one sentence reaches the accent pass whole. The 1995 and 1998
       builds end the stream instead and start another. */
    uint8_t  speak_now_inline;

    /* What a build does with speak now other than break the phrase. One: the
       Japanese build swallows the two characters and speaks the sentence as
       though they were absent, down to the sample. Two: the Russian build
       stops there the way it stops at a comma, and since it says nothing that
       does not end in a full stop, what is left is said as nothing. */
    uint8_t  speak_now_none;

    /* The contour shape the phrase header carries in its tenth byte, which
       the accent pass reads back. Most builds put 0x4C there; a few do not.
       Zero means 0x4C. */
    uint8_t  punct_s8[9];        /* phrase type per punctuation mark, 0 = English */
    uint8_t  punct_s9[9];        /* contour shape it forces, 0 = leave alone */
    uint8_t  hdr_shape;

    /* The shape the accent pass works from. Most builds read it back out of
       the phrase header; Spanish keeps the header's own byte for the rest of
       the engine and hands the accent pass 0x4C. Zero means the header's. */
    uint8_t  pat_ext;            /* the rule pattern has the cluster operators */

    /* One letter a build spells out before the rule pass rather than through
       it: Russian names its hard vowel this way. Zero means there is none. */
    uint8_t  lts_lit_char, lts_lit_code;

    /* How small the second nibble of a dictionary key has to be for a leading
       fifteen to be folded to one. Zero means the usual eight. */
    uint8_t  dict_lead_max;

    /* A build that rewrites its text into a Latin spelling before the rules
       see it. One means the Arabic set. The map turns its code page into the
       build's own numbering, and the two tables give the one or two Latin
       characters each of those stands for. */
    /* Whether a transition's duration byte is unsigned and its scaling is
       done in thirty-two bits. The 2006 builds do it that way, so a
       transition longer than a hundred and twenty-seven units still comes out
       positive; the 1995 build sign-extends the byte first. */
    /* Whether three sounds beyond the ones the attribute marks count as
       opening a group when the transition offsets are chosen. The 2006
       English build counts them; the other builds of that year do not. */
    /* Whether the German transition between two vowels keeps nineteen
       thirty-seconds of its duration rather than nine sixteenths. The 1998
       module does; the 2006 one does not. */
    /* The ceiling this build's pitch table reaches, where its own arithmetic
       does not fall out of the range commands. The Dutch module reaches a
       hundred and fifty-four where the other four reach a hundred and
       fifty-three. The 1995 and 1998
       Zero leaves the range commands to decide. */
    /* The sentence mode a build starts in, which decides the two percentages
       the pitch range is built from. The 1998 modules do not write it into
       the stream, so it has to be said here: the English one starts at four
       and the other five at three. Zero takes the stream's. */
    uint8_t  inton_mode;

    uint16_t voice_top;

    uint8_t  trn_between_wide;

    uint8_t  open_plain;

    uint8_t  trn_dur_wide;

    uint8_t  xlat_kind;
    uint8_t  xlat_map[0x100];
    uint32_t xlat_first, xlat_second;
    uint8_t  acc_kind;           /* 1 = the French accent pass */
    uint8_t  acc_shape;
    uint8_t  acc_shape_keep;    /* a single accent does not collapse the shape */
    uint8_t  run_stop_kind;    /* 1 = the run before an accent stops on its own test */
    /* A sound the run before an accent walks straight past, even though it
       carries a target of its own. Zero means none. */
    uint8_t  run_stop_skip;
    /* Most builds walk the run before a phrase-final break only when the
       accent it belongs to sits on a sound of its own; the 2006 French build
       walks it whatever the accent sits on, which is the difference between
       a question's trailing silence carrying its own record and not. */
    uint8_t  run_before_always;
    uint8_t  num_two_kind;     /* 1 = units, a joiner, then tens; 2 = no leading oh */
    uint8_t  num_group_kind;   /* 1 = a hundreds table, and a comma between groups */
    uint8_t  num_kind;         /* 1 = the Russian split by digit count, 2 = the German */
    uint8_t  num_spell_fives;  /* a long spelled run breaks into fives */
    uint8_t  no_edge_reduce;   /* the reduced vowel keeps its shape at a phrase edge */
    uint8_t  num_one_digit;    /* only a lone digit is read as a number */
    uint8_t  num_silent;       /* the build says nothing at all for a digit */
    uint8_t  pair_prescan;     /* the scan opens by stepping the group cursor */
    uint8_t  no_weak_mark;     /* an unstressed syllable is not given the weak mark */
    uint8_t  no_unrelease;     /* a stop between two vowels keeps its release */
    uint8_t  no_glottal;       /* no glottal stop between the two words */
    uint8_t  num_scale_kind;   /* 1 = the Italian plural thousand and its lone one */
    uint8_t  num_hundred_and;  /* a hundreds word joins what follows with "and" */
    uint8_t  num_liaison;      /* the marker a lone tens word is said behind */
    /* A four digit number that is not a round thousand is read as a count of
       hundreds, the hundred word kept in front of whatever follows it,
       rather than as a count of thousands. */
    uint8_t  num_four_as_hundreds;
    /* What the two lower "not this one" marks do, which the builds do not
       agree on: 0 the English way, 1 Russian, 2 the Romance and Greek and
       Hebrew way, 3 Japanese, 4 Arabic, 5 Dutch and German. */
    uint8_t  mark_kind;
    /* A syllable marked 0x78 takes the accent rather than shortening the
       run, unless another syllable asked for it outright. */
    uint8_t  mark_78_take;
    /* The accent mark an emphasised word takes, where it is not 0x35. */
    uint8_t  mark_emph;
    /* The build asks nothing of a syllable's own opener: every one of them
       is free to take the accent. */
    uint8_t  syl_free;
    /* The backward walk over the marks stops once a syllable has taken the
       accent, rather than running to the head of the word. */
    uint8_t  walk_stop;
    /* A reduced syllable standing behind an accented one has its vowel
       written over with the neutral one. */
    uint8_t  schwa_after;
    /* The accent is looked for forward from the first syllable, and falls
       back on it when every one of them refuses. */
    uint8_t  place_forward;
    /* The letters in front of a focus, when the rules ask for them: a list of
       the build's own prefixes, how many there are, and how far back the
       walk that collects them will go. */
    uint32_t prefixes;
    uint8_t  prefix_n, prefix_max;

    /* Where the compound splitter's five tries live. The word is cut where a
       run of consonants can be read as the end of one part and the start of
       the next, so the build carries, in one block, a limit, the roots of a
       suffix, a prefix, a coda, an unsplittable-cluster and an onset trie,
       and then the nodes themselves. Only German has one. */
    uint32_t split_tab;

    /* Letters this build writes doubled and reads single. The second of the
       pair goes before anything else sees the text, so neither the rules nor
       the dictionary ever meet it. */
    uint8_t  squash[4];

    /* The one sound a build shortens to a quarter when nothing but the phrase
       header stands in front of it. Only Spanish has one. */
    uint8_t  trn_lone;

    /* What the reader supplies once the text runs out. The 1995 build ends a
       text with a paragraph mark and two spaces; the 2006 builds put a full
       stop there instead, which is how an abbreviation that ends a text can
       still see a stop after it. Zero means the 1995 set. */
    uint8_t  tail_chars[4];

    /* Which syllable of a word carries the accent. Zero is the English rule
       in stress.c. One is the Romance rule: the last syllable but one, or the
       last when the word ends in a consonant that is not in stress_keep, and
       every other syllable flat rather than reduced. */
    uint8_t  long_ku;            /* the letter pair ku is written long */
    uint8_t  join_words;         /* words run together across spaces */
    uint8_t  stress_kind;        /* 1 = the Hebrew fallback and reduce */
    uint8_t  stress_rule;
    uint8_t  stress_keep[4];

    /* Whether the normaliser takes an English suffix off the word before it
       is looked up. Only the English builds do. */
    uint8_t  no_suffix;

    /* Which suffixes the normaliser takes off and what it puts back. One is
       the German set: a final e, or em, en, er or es, restored as the
       build's own three codes and a tail chosen by which one came off. */
    uint8_t  strip_kind;
    uint8_t  suffix_tail[8];

    /* Where the accent pass puts the closing fall when the last accent is a
       strong one and others follow it: on the next accent rather than in the
       last one's second slot. French, Japanese and Spanish do this. */
    uint8_t  fall_on_next;

    /* The code the accent pass writes for the closing fall. Zero means the
       usual 0x3D; the German build writes 0x3C. */
    uint8_t  fall_code;
    /* The mark the last accent of a statement takes, where it is not the
       usual one. */
    uint8_t  acc_fin;
    /* What a phrase of the first kind leaves behind it, where it is not the
       usual 0x42. */
    uint8_t  acc_code11;
    /* The fall a statement's last accent takes when it is also the last
       group of the phrase. */
    uint8_t  fall_code_last;

    /* Which events the pair scan emits for each of the module's own sound
       codes. Every build compiles a different switch here, because the same
       sound is not the same number twice and a language without aspirated
       stops has no case for them. Zero falls back to the English switch;
       tools/analysis/pairclass.py reads the rest out of the build. */
    uint8_t  pair_class[0x40];

    /* The characters the build rewrites as it reads the text: a few builds
       treat a plain letter as its accented form, and all of them fold the
       Latin-1 range into their own encoding. Empty means it takes the text as
       it stands. */
    uint8_t  in_map[0x100];

    /* The second character, for the few a build turns into two: German reads
       every h as ch. Zero means the one character stands alone. */
    uint8_t  in_map2[0x100];

    /* A build with letters written into its reader rather than into a table.
       One is the 2006 French set: three sequences it rewrites before it reads
       any of the text. */
    uint8_t  text_fix_kind;

    /* What each character in a rule pattern tests. A build that spells its
       patterns as plain letter-attribute bits -- Polish does, with no run or
       suffix operators at all -- gives the bit here and the matcher takes one
       character. Zero leaves the character to the English set in lts.c. */
    uint8_t  pat_bit[0x60];

    /* The accent level the build starts a text at. Zero means the usual
       three; Polish starts at two. */
    int8_t   init_level;

    /* Whether a transition's duration is halved whole. The English builds
       take a sixteenth off it first; the 2006 language builds do not. */
    uint8_t  trn_whole;
    /* The 1998 language builds scale a flanked or a between-sounds
       transition by tenths where the others use sixteenths. */
    uint8_t  trn_flank_tenths, trn_between_tenths;

    /* How much of a transition's duration is taken off before it is halved:
       a shift, defaulting to four. Italian takes a quarter. */
    uint8_t  trn_thin;

    /* Which of the vowel duration routines the build compiles. Zero is the
       English one in pairs.c; one is the shorter one six of the 2006 language
       builds share, which scales the table entry by the stress alone. */
    uint8_t  vdur_kind;

    /* The floor the vowel duration takes, and the range of sounds a longer
       floor of 0x37 applies to instead. */
    uint8_t  vdur_lo, vdur_hi;

    /* What a vowel's duration gains while the phrase is winding down. Zero
       means the Romance routine's 0x23. */
    uint8_t  vdur_slow;

    /* Whether a vowel at a phrase edge gains nothing. The Romance routine
       adds 0x32 there; Portuguese does not. */
    uint8_t  vdur_flat;

    /* Which of the transition routines the build compiles. Zero is the
       English one in pairs.c; one is the shorter one the 2006 language builds
       share, which drops the weak and paired cases and the pitch nudge. */
    uint8_t  trn_kind;

    /* Which phonological rules the build compiles. Zero is the English set in
       phrules.c; one is the Romance set, which softens the voiced stops
       between sounds, assimilates the sibilant, and lowers a weak vowel's
       stress mark next to another vowel. */
    uint8_t  ph_kind;

    /* A comma ends the text outright in the 2006 builds: what follows it is
       never spoken, and the comma itself is said with a full stop's pause and
       a full stop's sentence type. */
    uint8_t  comma_ends_text;

    /* The 2006 builds read a possessive as two words: "dog's" is "dog is".
       The earlier ones put the possessive's own sound on the end of the
       word. */
    uint8_t  possessive_is;

    /* What a transition's stored duration is multiplied by on its way to a
       sample count. Fifty-six in the earlier builds, fifty-three in the 2006
       ones. */
    uint8_t  dur_mult;

    /* The 2006 builds add half a step before every shift in the gain and
       pitch smoothers, so those round to nearest where the earlier builds
       truncate. */
    uint8_t  nearest_round;

    /* The numerator a pitch frequency is divided into to get a period in
       samples: the build's nominal sample rate. Eleven thousand and
       twenty-five in the earlier builds, ten thousand four hundred in the
       2006 ones, which is also why their transition multiplier is smaller. */
    uint16_t pitch_rate;

    /* The two constants that turn an intonation record's slope and duration
       into a sample count. They scale with the sample rate: 177 and 940 in
       the earlier builds, 167 and 887 in the 2006 ones. */
    uint16_t slope_mult, inton_dur_mult;

    /* A record with no duration of its own spreads its slope over the time
       left. The earlier builds divide by eighty-five; the 2006 ones multiply
       by three and shift down eight, which is eighty-five and a third. */
    uint8_t  inton_slope_shift;

    /* A transition whose sound is weak or sits between vowels keeps six or
       seven tenths of its duration. The 2006 builds reach those fractions by
       shifts -- nineteen over thirty-two and eleven over sixteen -- which are
       not quite the same numbers. */
    uint8_t  dur_frac_shift;

    /* The pitch table spreads the range between the middle and the top over
       ten steps. The 2006 builds round each step to nearest where the earlier
       ones truncate. */
    uint8_t  contour_round;

    /* The 1998 build halves a vowel's duration on the way out of the pair
       scan, after the floors rather than before them, and rounds the
       transition's own halving down where the 1995 build rounds it up. */
    uint8_t  vowel_dur_shift, trn_round;

    /* How far this language's marks sit above English's. The six 1998 modules
       are one body of code compiled once each with a different number of
       sounds, so a sound code means the same thing in all of them and every
       code above the inventory -- the stress marks, the phrase markers, the
       command byte -- is shifted by the difference. The library keeps its
       streams in English numbering and shifts only where it indexes one of the
       engine's own tables by a code. */
    int8_t   code_shift;

    /* The command byte, which the 2006 builds do not always number with the
       rest of the marks: it sits at the top of the mark set, so a language
       with fewer marks puts it lower than its inventory alone would say.
       Zero means the shift applies to it like anything else. */
    uint8_t  cmd_code;

    /* Targets per voice. The diphone segment holds every voice's targets
       before the records, so this says where the records start as well as how
       to reach a voice above the first. */
    uint16_t voice_span;
    /* Bytes per stored coefficient. The 2006 builds widen each to four and
       read the low byte, and they carry one voice, so their span is zero. */
    uint8_t  voice_stride;
    /* The 2006 build's duration scaling is the same arithmetic reached by
       shifts: a byte table over a power of two where the earlier builds hold
       a numerator and a denominator, and the divisions around it open-coded
       as arithmetic shifts, which floor where a divide truncates. Zero means
       the pair table and the divides. */
    uint8_t  stress_shift;

    /* the dictionary. A build that holds the two counts as immediates rather
       than in memory leaves the address zero and gives the count instead. */
    uint32_t code_medial, code_initial;
    uint32_t ph_single, ph_single_max, ph_pair, ph_pair_max;
    int16_t  ph_single_n, ph_pair_n;
    uint32_t bucket_index[BST_BUCKETS], bucket_data[BST_BUCKETS];
    /* letter to sound */
    uint32_t suffix_ptrs, lts_index, dispatch, rules, patterns, outputs;
    /* This build's pointer tables hold near pointers, an offset into the
       table's own segment with the fixup word beside it, rather than whole
       addresses. */
    uint8_t  near_ptrs;
    /* The letter a dictionary key drops from the end and records in its
       flag: 'e' in English, and its own in each of the others. */
    uint8_t  dict_silent;
    /* The sounds a syllable is built on that refuse the accent: the first
       set refuses it outright, the second only in the reduction. */
    uint8_t  syl_weak[4], syl_flat[4];
    /* How many suffixes the pattern's run of letters is tried against, and
       how long that run may be. */
    uint8_t  suffix_n, suffix_max;
    /* the exception trie */
    uint32_t trie_desc;
    /* the record-to-stream conversion */
    uint32_t modmap, modtab;
    /* Where the rows the modifier table points at live, when that is not the
       segment the table itself is in. */
    uint32_t modtab_rows;
    /* the pair scan */
    uint32_t trans_pitch, vowel_dur, stress_num, stress_add, sound_add;
    /* diphones and voices */
    uint32_t diph_records, diph_offsets, diph_offsets_end, voices;

    uint32_t h[BST_H_COUNT];
    uint32_t s[BST_S_COUNT];
} bst_tabmap;

extern const bst_tabmap BST_MAP_1995;
extern const bst_tabmap BST_MAP_1998_ENG;
extern const bst_tabmap BST_MAP_1998_DUT;
extern const bst_tabmap BST_MAP_1998_FRN;
extern const bst_tabmap BST_MAP_1998_GRM;
extern const bst_tabmap BST_MAP_1998_ITL;
extern const bst_tabmap BST_MAP_1998_SPN;
extern const bst_tabmap BST_MAP_2006_ENG;
extern const bst_tabmap BST_MAP_2006_ARA;
extern const bst_tabmap BST_MAP_2006_DUT;
extern const bst_tabmap BST_MAP_2006_FRE;
extern const bst_tabmap BST_MAP_2006_GER;
extern const bst_tabmap BST_MAP_2006_GRE;
extern const bst_tabmap BST_MAP_2006_HEB;
extern const bst_tabmap BST_MAP_2006_ITA;
extern const bst_tabmap BST_MAP_2006_JPN;
extern const bst_tabmap BST_MAP_2006_POL;
extern const bst_tabmap BST_MAP_2006_POR;
extern const bst_tabmap BST_MAP_2006_RUS;
extern const bst_tabmap BST_MAP_2006_SPA;

/* Fills names with the entries the map has not been given and returns how
   many there were, so an incomplete build can say so rather than misbehave. */
int bst_map_gaps(const bst_tabmap *m, const char **names, int max);
const bst_tabmap *bst_map_named(const char *name);


/* One section of the image: where it lands in memory and where its bytes are
   in the file. */
typedef struct { uint32_t va, vsize, raw, rawsize; } bst_section;

typedef struct {
    const uint8_t *image;
    size_t         len;
    /* The section map, so an address in the image can be followed wherever it
       points. Most tables live in .rdata, but the pronunciation modifiers are
       a table of pointers into .data. */
    uint32_t base;
    int      nsec;
    bst_section sec[BST_SECTIONS];
    bst_tabmap t;
    /* Set when the image is one the library built rather than one the caller
       handed over: a 16-bit module has to be laid out and relocated before
       any of its pointers mean anything. */
    void *own;
} bst_image;

/* Set to print each stage's working as it runs -- the stream as assembled,
   after the rule pass and at the pair scan, the transition records, the vowel
   durations and the gain smoother's inputs. What a build is compared against
   the engine on when its frames stop agreeing.

   It is a variable so a driver can turn it on part way through a run, which
   also means the calls reach the linker in every ordinary build and drag
   vfprintf in behind them. BST_NO_TRACE makes it a constant instead, so the
   blocks fold away and nothing in the library names stdio at all. */
#ifdef BST_NO_TRACE
enum { bst_trace = 0 };
#define bst_tracef(...) ((void)0)
#else
extern int bst_trace;
#define bst_tracef(...) fprintf(stderr, __VA_ARGS__)
#endif

/* Returns a pointer to `need` bytes at a virtual address, or NULL. */
const uint8_t *bst_at(const bst_image *img, uint32_t va, size_t need);

/* Set to watch every read that resolves, by the offset into the image it
   landed on. This is how the lifted tables are worked out: run the corpus
   with it set and what it saw is what the build needs. */
extern void (*bst_read_hook)(const bst_image *img, size_t off, size_t need);

/* The three accessors every table read goes through. An address outside the
   image reads as zero, which is what the original does when a table index runs
   off the end of its table into whatever follows. */
static inline int bst_u8(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i, 1);
    return p ? p[0] : 0;
}
static inline int bst_u16(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i * 2, 2);
    return p ? (p[0] | (p[1] << 8)) : 0;
}
static inline int bst_s16(const bst_image *img, uint32_t va, int i) {
    return (int16_t)bst_u16(img, va, i);
}
static inline uint32_t bst_u32(const bst_image *img, uint32_t va, int i) {
    const uint8_t *p = bst_at(img, va + (uint32_t)i * 4, 4);
    return p ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)) : 0;
}

/* Suffixes the normaliser strips, recorded so a later stage can restore the
   sound. */
#define BST_SUF_ED   0x80
#define BST_SUF_ING  0x40
#define BST_SUF_LY   0x20
#define BST_SUF_S    0x10
#define BST_SUF_APOS 0x08

#define BST_XLAT_MAX 2048

/* Rewrites a build's own code page into the Latin spelling its rules read.
   Returns the length, or -1 if the build does not do this. */
int bst_translit(const bst_image *img, const char *in, char *out, int max);
#define BST_WORD_MAX 96

typedef struct {
    char    buf[BST_WORD_MAX];   /* underscore-delimited, e.g. "_hope__" */
    int     len;                 /* characters up to and including the stem */
    int     flags;               /* which suffix came off */
    int     vowels;              /* vowel count, the engine's syllable proxy */
    int     y_from_i;            /* set when -ly stripping rewrote an i as y,
                                    which changes how the suffix is restored */
} bst_word;

int  bst_image_init(bst_image *img, const void *data, size_t len);
/* The same, with the table directory given rather than assumed. */
int  bst_image_init_map(bst_image *img, const void *data, size_t len,
                        const bst_tabmap *map);

/* A 16-bit NE module. Its segments are laid out one to a 64K window and its
   relocations applied, because a pointer in the file holds a link in a fixup
   chain rather than an address. Addresses are then (segment << 16) | offset,
   which is what an applied internal relocation writes, so a pointer read out
   of the loaded image is already in the form bst_at wants.

   Call bst_image_free when done; the other two initialisers need no freeing
   but tolerate it. */
int  bst_image_init_ne(bst_image *img, const void *data, size_t len,
                       const bst_tabmap *map);
void bst_image_free(bst_image *img);

/* The tables a build needs, taken out of its binary and compiled in. Only the
   bytes the engine ever reads are kept, so the image is put back as a run of
   chunks at the offsets they came from and nothing else. Reading it is the
   same as reading the original: the section map and the base are the ones the
   binary had, so every address in the map still lands where it did. */
typedef struct { uint32_t off, len; const uint8_t *bytes; } bst_chunk;

typedef struct {
    uint32_t           base;
    uint32_t           size;      /* how much of the image the chunks reach */
    int                nsec;
    const bst_section *sec;
    int                nchunk;
    const bst_chunk   *chunk;
    /* The lattice tables, already unpacked, because a 16-bit module's are at
       file offsets and the laid-out image is not the file. */
    const void        *lat;
} bst_lifted;

/* Lays the chunks out and points the image at them. Needs bst_image_free. */
int  bst_image_init_lifted(bst_image *img, const bst_lifted *d,
                           const bst_tabmap *map);

/* The lifted tables for a build, or NULL if this library was built without
   them. Written by tools/lift. */
const bst_lifted *bst_lifted_for(const char *build);

/* A phoneme record: a type letter and two operands, which is the form the
   rest of the engine consumes. */
typedef struct {
    uint8_t type;      /* 'T' 'X' 'A' 'S' 'V' 'C' 'P' */
    uint8_t a, b;
} bst_rec;

#define BST_RECS_MAX 64

typedef struct {
    bst_rec rec[BST_RECS_MAX];
    int     n;
} bst_recs;

/* Looks a normalised word up in the dictionary. Returns 1 and fills out on a
   hit, 0 on a miss, in which case the caller should fall back to the rules. */
int  bst_dict_lookup(const bst_image *img, const bst_word *w, bst_recs *out);

/* The rule path produces a phoneme code stream rather than typed records: a
   code that opens a group occupies the middle of three slots, and modifiers
   are written into the slots either side of it. The dictionary's typed records
   are converted into this same form by a later stage. */
#define BST_STREAM_MAX 128

typedef struct {
    uint8_t buf[BST_STREAM_MAX];
    int     len;                 /* bytes written, excluding the leading slot */
} bst_stream;

/* The stream builder both word paths write through. A sound that opens a
   syllable takes four slots, itself and three after it, and a sound that does
   not opens none: it goes into whichever of the open slots its attributes
   select, or is appended. */
typedef struct {
    unsigned char buf[128];
    int pos, before, at, after, stopped;
} bst_builder;

void bst_build_init(bst_builder *b);
void bst_build_emit(const bst_image *img, bst_builder *b, int code);
void bst_build_suffix(const bst_image *img, bst_builder *b, int flags, int y_from_i);
void bst_lts_build(const bst_image *img, const bst_word *w, bst_builder *b,
                   int indict);

/* ---- sentence assembly --------------------------------------------------

   Tokens in, one phrase's phoneme stream out. */

typedef struct {
    const bst_image *img;
    uint8_t *s;
    int      wp, hdr, last, len;
    int      emph, punct, hist, mode;
    int      done, full, open;
    int      stuck;    /* a word was written past a break the opening brace
                          held open, which the 2006 builds never recover from */
    uint8_t  carry[6];
} bst_assembler;

void bst_assemble_init(bst_assembler *z, const bst_image *img, uint8_t *stream);
void bst_assemble_start(bst_assembler *z);
/* Returns non-zero once the phrase is complete. */
int  bst_assemble_token(bst_assembler *z, int kind, uint8_t *buf);

/* Chooses which syllable of a word carries the accent and fills in every
   syllable's mark. `emph` is the emphasis state the surrounding text set and
   `mode` the engine's mode bits. */
void bst_word_stress(const bst_image *img, uint8_t *stream, int len,
                     int emph, int mode);

/* Applies the dictionary's typed records to a code stream, which is the form
   the rest of the engine wants. The two flags carry the "a main stress has
   been placed" and "a secondary has" state across the stem and the suffix, so
   a suffix that wants the stress can take it off the stem.

   Returns the new stream length, or -1 on overflow. */
int  bst_recs_to_stream(const bst_image *img, const bst_rec *rec, int nrec,
                        bst_builder *b, int suffix,
                        int *stress_seen, int *accent_seen);

/* Applies the phonological rule pass to a sentence phoneme stream in place.
   The stream may grow, up to cap: *len is updated and the number of insertions
   returned, because anything the caller holds an offset into shifts by that
   much. */
int  bst_phrules(const bst_image *img, uint8_t *stream, int *len, int cap, int level);

/* Pronounces a word the dictionary does not hold, by the letter-to-sound
   rules. Always succeeds: the rule set has a default for every letter. */
void bst_lts(const bst_image *img, const bst_word *w, bst_stream *out);

/* The whole word path in one call: normalise, look the word up, fall back to
   the rules, and restore the stripped suffix. Returns 1 if the pronunciation
   came from the dictionary, 0 if from the rules.

   Dictionary hits come back as typed records and rule results as a code
   stream, because that is how the engine keeps them; the caller gets whichever
   applies and recs->n is zero when the stream was used. Converting one into
   the other is a later stage that is not reproduced yet. */
int  bst_word_pronounce(const bst_image *img, const char *word,
                        bst_recs *recs, bst_stream *stream);

/* Copies a word into the engine's working form and strips one inflectional
   suffix. Mirrors the original exactly, including that the stem check can
   collapse a doubled consonant, restore a silent e, or refuse the strip. */
void bst_normalise(const bst_image *img, const char *word, bst_word *out);
int  bst_word_split(const bst_image *img, const char *word, int *cut, int max);

/* ---- the sentence stream ------------------------------------------------

   Everything past the word front end works on one byte stream per sentence,
   scanned by a handful of cursors that skip empty slots and step over the
   six-byte command records. The stages share the cursors, so they live here
   rather than inside any one of them. */

typedef struct { int val, pos; } bst_cur;

/* A stream code as this build's tables index it. Sounds are numbered the same
   everywhere; marks are not. */
/* English's inventory is forty-eight sounds, 0x00 to 0x2F, and the last of
   them is the pause. A language with more sounds puts its pause and every
   mark above it further up, so the boundary is the pause itself, and the
   library's streams keep English's numbering and shift only here.
 *
 * A language with more sounds than English has some that English has no
 * number for. Those sit in the stream with the top bit set, out of the way of
 * every mark, and come back to the module's own numbering here. */
static inline int bst_code(const bst_image *img, int c) {
    if (c >= 0x80) return (c - 0x80 + 0x2F) & 0xFF;
    if (c == 0x7C && img->t.cmd_code) return img->t.cmd_code;
    return c >= 0x2F ? (c + img->t.code_shift) & 0xFF : c;
}

/* The other way: a code read out of the image, in the module's own numbering,
   turned into the numbering the library keeps its streams in. */
static inline int bst_uncode(const bst_image *img, int c) {
    if (img->t.cmd_code && c == img->t.cmd_code) return 0x7C;
    if (img->t.code_shift == 0) return c;
    int pause = (0x2F + img->t.code_shift) & 0xFF;
    if (c >= pause) return (c - img->t.code_shift) & 0xFF;
    if (img->t.code_shift > 0 && c >= 0x2F) return 0x80 + (c - 0x2F);
    return c;
}

/* Whether a byte in a stream is a sound rather than a mark. */
static inline int bst_is_sound(int c) { return (c >= 1 && c <= 0x30) || c >= 0x80; }

/* The two phoneme attribute bytes. The first classifies the sound; the second
   says which of the scans below stop on it. */
int bst_ph_attr1(const bst_image *img, int c);
int bst_ph_attr2(const bst_image *img, int c);

/* Forward to the next segment, stress mark or eighth-class sound; backward to
   the previous eighth-class sound; and forward to the next segment that opens
   a group. Each gives up slightly differently at the end of the stream, which
   is load-bearing: a cursor that reads zero because its scan found nothing is
   distinguishable from one that found a zero. */
void bst_scan_seg(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_stress(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_eight(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);
void bst_scan_eight_back(const bst_image *img, const uint8_t *s, int from, bst_cur *c);
void bst_scan_strong(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c);

/* ---- the pair scan ------------------------------------------------------

   One traversal of the sentence stream emitting, for each sound, a diphone
   segment naming it and the sound before it, and up to three transition
   events around it carrying duration and two pitch offsets. How many events
   fall before the segment and how many after is fixed per sound class, and
   the count of events since the last segment is what the segment records, so
   the two kinds interleave and cannot be produced separately. */

#define BST_EMIT_SEG   0
#define BST_EMIT_TRANS 1

typedef struct {
    uint8_t  kind;
    uint16_t index;   /* segment: previous * 48 + current, or a command with
                         0xF000 set; transition: (sound - 1) * 3 + position */
    uint16_t count;   /* segment: events since the last one; transition: the
                         duration, before the engine clamps it to a byte */
    int16_t  a, b;    /* segment: the command's operands; transition: the two
                         pitch offsets, 0x7F meaning "no target" */
} bst_emit;

/* State the scan carries between sentences. */
typedef struct {
    int prev;        /* the sound the next sentence's first pair starts from */
    int strong;      /* the group-opening cursor's value, kept across calls */
    int emphasis;
    int flags;
} bst_pair_state;

/* Returns the number of records written, or -1 if max was too small. */
int bst_pairs(const bst_image *img, const uint8_t *stream, int len,
              bst_pair_state *st, bst_emit *out, int max);

/* ---- accent assignment --------------------------------------------------

   Fills the empty slots after each group opener with pitch-target codes, which
   is the input the contour generator reads. Returns the number of accents the
   sentence has. */

typedef struct {
    int flags;      /* the stream's own mode bits */
    int carried;    /* the level the previous phrase ended on, in and out */
    int level;      /* the voice's pitch level index, in and out */
    int tail;       /* offset of the slot that hands a level on, or -1 */
    int emphasis;   /* set on return if an emphasis command was in force */
} bst_accent_state;

int bst_accents(const bst_image *img, uint8_t *stream, int len,
                bst_accent_state *st);

/* ---- the intonation contour ---------------------------------------------

   The voice's pitch table spans its range in fourteen steps; an accent code is
   an index into it. The contour generator walks the accents emitting one
   record each: the pitch period to head for, how many segments to take getting
   there, and the shape of the move. */

typedef struct {
    int   base;        /* the voice's floor, in hertz */
    int   top;         /* the stored ceiling the range commands scale */
    int   voicebase;   /* the floor to fall back to */
    int   level;       /* the default pitch level index */
    int   emphasis;
    int   voice;
    int   strong;      /* the group cursor's value, carried between calls */
    int   mid, hi;     /* the current middle and ceiling */
    int   round10;     /* half a step, added before the table's division */
    int   reach;       /* the ceiling a build reaches whatever the range says */
    int   levmax;      /* the highest level its accent codes name */
    short table[14];
} bst_voice;

typedef struct {
    uint8_t kind;      /* 2 a move, 1 the close */
    uint8_t period;
    int16_t dur;
    int16_t slope;
} bst_contour_rec;

int bst_contour(const bst_image *img, const uint8_t *stream, int len,
                bst_voice *v, bst_contour_rec *out, int max);

/* ---- diphone expansion --------------------------------------------------

   A segment's index names an entry in the diphone inventory, and the entry
   holds one sub-sequence of acoustic targets per position the sound covers.
   The segment's count says how many positions to take. */

int  bst_diphone_count(const bst_image *img);
int  bst_diphone(const bst_image *img, int index, int positions,
                 uint16_t *out, int max);
void bst_target_coeffs(const bst_image *img, int voice, int target, int16_t k[10]);

#endif
