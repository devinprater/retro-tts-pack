/*
 * The Spanish engine object.
 *
 * This is a second decompilation, not a translation of the English one: the
 * November 1995 engines and the October 1997 English engine are different
 * builds with different structure layouts, so nothing here may be assumed
 * from src/ without checking it against CGRM_ES.DLL.  docs/SPANISH.md says
 * how each fact was established and lang/spa/engine/engine.fields carries the layout.
 *
 * Address annotations (/ * @0x1000e5a0 * /) name the function's address in
 * CGRM_ES.DLL, which is what tools/gen_hookmap.py binds for the hook build.
 * They are Spanish addresses; the identical-looking ones in src/ are not.
 */
#ifndef TV_ES_ENGINE_H
#define TV_ES_ENGINE_H

#include "tv_common.h"
/* Stored addresses in the engine's data are four bytes whatever a pointer is
 * on the host, so every table of them is a table of tv_ref.  See src/tv_ref.h. */
#include "tv_ref.h"

#include "es_engine_struct.h" /* generated from lang/spa/engine/engine.fields */

/* OpenTV: on, the pitch ceiling is the English engine's 0x1f4 rather than this
 * engine's 0x32..0xc8, which two of the ten voices sit above.  Set from
 * TVTTS_EXT_PITCH; see lang/spa/engine/stage2.c. */
extern int tv_es_ext_pitch;
extern int tv_es_ext_contour;
extern int tv_es_ext_floor;
extern int tv_es_ext_rate;
/* (400 - 46) >> 3; the row Engine_SetSpeed will not go past. */
#define ES_RATE_ROW_MAX_PUBLIC 44

/* OpenTV's third output rate.  The original offered 8 kHz and 11.025 kHz; this
 * one is an addition, and the tables for it are computed from the formulas that
 * reproduce both of the original's exactly.  See src/syn_hifi.h. */
#define TV_SR_HIFI_ES 16000

/* in_ring and mid_ring are 0x1000; pre_ring is 0x100 (Preformat_Run masks
 * its index with 0xff). */
#define TV_ES_RING_SIZE 0x1000

/* ---- the character rings (ring.c) ---------------------------------------- */

/* @0x1000e5a0 */
int32_t TV_THISCALL Engine_InFree(Engine *self);
/* @0x1000e5c0 */
int32_t TV_THISCALL Engine_InGet(Engine *self);
/* @0x1000e600 */
uint8_t TV_THISCALL Engine_InUnget(Engine *self);
/* @0x1000e630 */
uint8_t TV_THISCALL Engine_InPut(Engine *self, uint8_t c);
/* @0x1000e680 */
uint8_t TV_THISCALL Engine_InPutEnd(Engine *self);
/* @0x1000e4f0 */
int32_t TV_THISCALL Engine_MidFree(Engine *self);
/* @0x1000e510 */
int32_t TV_THISCALL Engine_MidGet(Engine *self);
/* @0x1000e550 */
uint8_t TV_THISCALL Engine_MidPut(Engine *self, uint8_t c);

/* ---- moving input onward (flush.c) --------------------------------------- */

/* @0x1001c710 */
void TV_THISCALL Engine_Flush(Engine *self, int32_t new_item);

/* ---- the preformatter (preformat.c) -------------------------------------- */

/* @0x10008060 */
void TV_THISCALL Preformat_PutChar(Engine *self, uint8_t c);

/* ---- the input stage (input.c) ------------------------------------------ */

/* Node types: the low three bits of Node.flags. */
#define NODE_TYPE_MASK 7u
#define NODE_CONTROL   0u
#define NODE_TYPE(n)   ((n)->flags & NODE_TYPE_MASK)
#define NODE_FREE      6u
#define NODE_SENTINEL  7u

/* Engine_ResetNodes links 0x26a of them, and 618 * 0x1c from 0x928 ends
 * exactly where in_ring begins. */
#define TV_ES_NODE_POOL 618

/* @0x1000e290 */
uint8_t TV_THISCALL Engine_InputStage(Engine *self);
/* Takes one TextData item from the host into the input ring, cutting it into
 * lines and closing each as a sentence when the PreFormat option is on. */
/* @0x1001c310 */
void TV_THISCALL Engine_Feed(Engine *self, const char *text, uint32_t len,
                             uint32_t *ppos);

/* ---- the node pool (node.c) ---------------------------------------------- */

/* @0x10008bd0 */
Node *TV_THISCALL Engine_NodeAlloc(Engine *self, Node *ref, int32_t after,
                                   int32_t type, uint8_t value);
/* @0x10008b60 */
Node *TV_THISCALL Engine_AppendNode(Engine *self, int32_t type, int32_t value);
/* @0x10008dc0 */
Node *TV_THISCALL Engine_NodeFree(Engine *self, Node *n, int32_t forward);

/* ---- list surgery (list.c) ----------------------------------------------- */

/* @0x10009110 */
Node *TV_THISCALL Engine_Unlink(Engine *self, Node *n);
/* @0x10008ed0 */
Node *TV_THISCALL Engine_InsertBefore(Engine *self, Node *n, Node *before);

/* ---- the escape parser (escape.c) ---------------------------------------- */

/* @0x10007810 */
void TV_THISCALL Preformat_Run(Engine *self);
void Engine_ZeroDwordIfMinus1(Engine *self, uint32_t off32);

/* ---- leaf utilities (util.c) --------------------------------------------- */

/* @0x1001da50 */
int32_t TV_CDECL Bits_Test(int32_t bit, const uint32_t *bits);
/* @0x1001da90 */
uint32_t TV_CDECL Bits_Set(int32_t bit, uint32_t *bits);
/* @0x1001dac0 */
uint32_t TV_CDECL Bits_Clear(int32_t bit, uint32_t *bits);
/* @0x1001daf0 */
int32_t TV_CDECL Bits_Next(int32_t bit, const uint32_t *bits);
/* @0x1001a930 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n);
/* @0x1001a9b0 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n);
/* @0x1001a960 */
uint8_t TV_STDCALL Phone_TestMask(Node *n, int32_t mask, int32_t neg);
/* @0x1000aee0 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi);
/* @0x1000af00 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b);

/* @0x1000aed0 */
int32_t TV_STDCALL Synth_MulShr11(int32_t a, int32_t b);
/* @0x1000ea70 */
uint8_t TV_THISCALL Synth_Gate(Engine *self, int32_t op);
/* @0x1001db60 */
int32_t TV_CDECL Bits_AllIn(const uint32_t *need, const uint32_t *have);
/* @0x1001db90 */
int32_t TV_CDECL Bits_AnyIn(const uint32_t *want, const uint32_t *have);
/* @0x1001bfd0 */
int32_t TV_STDCALL Synth_ScaleParam(int32_t index, uint8_t raw);

/* The Spanish five vowels.  Vowel_Index exists twice in the image, byte for
 * byte the same; see the note in util.c. */
/* @0x100132b0 */
int32_t TV_STDCALL Vowel_Index(uint8_t c);
/* @0x100121f0 */
int32_t TV_STDCALL Vowel_Index2(uint8_t c);
/* @0x100132e0 */
uint8_t TV_STDCALL Is_Vowel(uint8_t c);
/* A Roman numeral's value, or -1 when the string is not one. */
/* @0x10022380 */
int32_t TV_CDECL Roman_Value(const char *s);
/* Latin-1 in, the letter under the accent out, and the mark it carried left
 * in the caller's byte so the next read picks it up. */
/* @0x10014db0 */
uint8_t TV_CDECL Accent_Split(uint8_t *p);
/* The same for the other table pair, keeping the letter's case. */
/* @0x10020cf0 */
uint8_t TV_CDECL Accent_Fold(uint8_t *p);

/* ---- the TextIn tokenizer (textin.c) -------------------------------------- */

/* @0x1001dc10 */
int32_t TV_THISCALL TextIn_GetChar(TextIn *self);
/* @0x1001dc50 */
int32_t TV_THISCALL TextIn_Unget(TextIn *self);
/* @0x1001dc70 */
int32_t TV_THISCALL TextIn_PutString(TextIn *self, const char *s);
/* @0x1001dbc0 */
int32_t TV_CDECL AllocString(char **p, int32_t n);
/* @0x1001d960 */
Token *TV_THISCALL TextIn_RemoveToken(TextIn *self, Token *t, int32_t dir);
/* @0x1001d740 */
Token *TV_THISCALL TextIn_InsertAfter(TextIn *self, Token *ref);
/* @0x1001d7c0 */
Token *TV_THISCALL TextIn_InsertBefore(TextIn *self, Token *ref);
/* @0x1001d850 */
Token *TV_THISCALL TextIn_Reattach(TextIn *self, Token *ref, int32_t dir);
/* @0x1001c950 */
int32_t TV_THISCALL TextIn_Tokenize(TextIn *self);
/* @0x1001c8a0 */
int32_t TV_THISCALL TextIn_Flush(TextIn *self, int32_t final);
/* @0x1001fdd0 */
int32_t TV_THISCALL TextIn_Unmatched(TextIn *self, Token *t);

/* ---- the C runtime the engine calls --------------------------------------
 * Statically linked into CGRM_ES.DLL, so the hook build binds these to the
 * DLL's own copies and memory from its heap is always freed by its heap.
 * The addresses are Spanish; src/crt.h has the English ones. */

/* @0x10023a06 */
void *TV_CDECL tv_malloc(size_t n);
/* @0x1002397d */
void TV_CDECL tv_free(void *p);
/* @0x100235d7 */
void *TV_CDECL tv_new(size_t n);
/* @0x1002d21e */
char *TV_CDECL tv_itoa(int32_t value, char *buf, int32_t radix);
/* @0x100237b8 */
char *TV_CDECL tv_strchr(const char *s, int32_t c);
/* @0x1002449d */
int32_t TV_CDECL tv_atoi(const char *s);
/* @0x10024408 */
int32_t TV_CDECL tv_atol(const char *s);
/* @0x100244ac */
char *TV_CDECL tv_strstr(const char *s, const char *sub);
/* @0x10023b51 */
char *TV_CDECL tv_strlwr(char *s);

/* the tokenizer proper */
/* @0x1001c9e0 */
int32_t TV_THISCALL TextIn_ReadToken(TextIn *self, Token **out);
/* @0x1001d260 */
int32_t TV_THISCALL TextIn_Split(TextIn *self, Token **t);
/* The same tokenizer over a string rather than the input ring, splitting each
 * token as it goes; `mark` puts flag 3 on every one of them. */
/* @0x1001cd10 */
int32_t TV_THISCALL TextIn_TokenizeText(TextIn *self, const char *text,
                                        int32_t mark);
/* @0x1001d470 */
int32_t TV_THISCALL TextIn_Mode4(TextIn *self, Token *t);
/* Puts a string into the list as a new token's replacement text, before the
 * token given when dir is -1 and after it otherwise. */
/* @0x1001d8d0 */
int32_t TV_THISCALL TextIn_InsertText(TextIn *self, Token *ref,
                                      const char *text, int32_t dir);
/* @0x1001c6c0 */
uint8_t TV_THISCALL Engine_CreateTextIn(Engine *self);
/* @0x1001c790 */
TextIn *TV_THISCALL TextIn_Construct(TextIn *self, int32_t mode);
/* @0x1001c850 */
int32_t TV_THISCALL TextIn_Reset(TextIn *self);
/* @0x1001e0d0 */
int32_t TV_THISCALL TextIn_Advance(TextIn *self);
/* Reads one ESC[ sequence into a token of its own, or puts the characters
 * back when it is not one. */
/* @0x1001d090 */
int32_t TV_THISCALL TextIn_ReadEscape(TextIn *self, const char *base,
                                      int32_t *ppos, int32_t no_mode4);
/* Reads one line into a buffer, giving the index of its first space. */
/* @0x100230b0 */
int32_t TV_THISCALL TextIn_ReadLine(TextIn *self, char *buf,
                                    int32_t *first_space);
/* Reads the headers of a mail message, when mode 4 says the text is one. */
/* @0x10022970 */
int32_t TV_THISCALL TextIn_Mode4Reset(TextIn *self);
/* The three headers mode 4 speaks.  Mode4_Header takes 1 for Subject, 2 for Cc
 * and 3 for Bcc, and refuses anything else.  Both of the others write into the
 * text they are given. */
/* @0x10022cf0 */
int32_t TV_THISCALL Mode4_From(TextIn *self, char *text);
/* @0x10022e90 */
int32_t TV_THISCALL Mode4_Header(TextIn *self, const char *text,
                                 int32_t which);
/* @0x10022f90 */
int32_t TV_THISCALL Mode4_Date(TextIn *self, char *text);
/* Which rule tables a token's flags select, and the ESC[nX mode change. */
/* @0x1001e160 */
int32_t TV_THISCALL Rule_Select(TextIn *self, Token *t, int32_t *bits,
                               int16_t **tables);
/* Tells the SAPI object the tokenizer's mode. */
/* @0x1001dcc0 */
void TV_THISCALL TextIn_PublishMode(TextIn *self);
/* @0x1001e490 */
int32_t TV_THISCALL Rule_Run(TextIn *self, Token **t);
/* @0x1001f850 */
int32_t TV_THISCALL TextIn_Emit(TextIn *self, int32_t final);
/* @0x1000e120 */
void TV_THISCALL Queue_Push(void *queue, void *data, int32_t len);

/* ---- the engine object's life (engine.c) ---------------------------------- */

/* @0x1000ddc0 */
Engine *TV_THISCALL Engine_Construct(Engine *self);
/* @0x100086a0 */
int32_t TV_THISCALL Engine_Init(Engine *self);
/* @0x10008700 */
int32_t TV_THISCALL Engine_Reset(Engine *self);
/* @0x1000e240 */
void TV_THISCALL Engine_ResetRings(Engine *self);
/* @0x10008ef0 */
void TV_THISCALL Engine_ResetNodes(Engine *self);
/* @0x100087d0 */
int32_t TV_THISCALL Engine_Step(Engine *self);

/* the reset chain and the stages */
/* @0x10007790 */
void TV_THISCALL Preformat_Reset(Engine *self);
/* @0x10017850 */
void TV_THISCALL Synth_ResetTracks(Engine *self);
/* @0x1000e6f0 */
void TV_THISCALL Synth_InitFilters(Engine *self);
/* @0x1000ad50 */
void TV_THISCALL Output_Reset(Engine *self, int16_t rate);
/* @0x1000f070 */
void TV_THISCALL Prosody_Reset(Engine *self);
/* @0x1001bf10 */
void TV_THISCALL Stage3_ResetParams(Engine *self);
/* @0x1001a8b0 */
void TV_THISCALL Stage2_Reset(Engine *self);
/* @0x1000fa10 */
void TV_THISCALL Stage1_Reset(Engine *self);
/* @0x10013360 */
uint8_t TV_THISCALL Stage0_Run(Engine *self);
/* @0x1000fa30 */
uint8_t TV_THISCALL Stage1_Run(Engine *self);
/* The same over the loanword lexicon. */
/* @0x10023250 */
uint8_t TV_THISCALL Stage1_Loanword(Engine *self);
/* Looks the word between stage 1's cursors up in the exception lexicon and
 * replaces it when it is there. */
/* @0x10001040 */
uint8_t TV_THISCALL Stage1_Lexicon(Engine *self);
/* One phoneme of stage 1: the Spanish spelling rules, one arm each. */
/* @0x1000fca0 */
uint8_t TV_THISCALL Stage1_Phoneme(Engine *self);
/* Looks ahead for the end of the word and puts a '&' at it; see lang/spa/engine/stage1.c
 * for what the four results mean. */
/* @0x10010480 */
int32_t TV_THISCALL Stage1_WordMark(Engine *self);
/* @0x1001a070 */
uint8_t TV_THISCALL Stage2_Run(Engine *self);
/* Appends one byte to a growable byte list; the host's, not the engine's. */
/* @0x1000df10 */
uint8_t TV_THISCALL ByteList_Add(void *list, int32_t byte);
/* @0x1001ad30 */
int32_t TV_THISCALL Stage3_Run(Engine *self);
/* @0x100077b0 */
uint8_t TV_THISCALL Synth_Step(Engine *self);
/* the frame generator and the parameter-list builder it sits between;
 * neither is written yet.  sub_1000f090 is the one the surviving trace
 * string calls ParL. */
/* @0x10009bf0 */
void TV_THISCALL Synth_Generate(Engine *self, uint16_t rate,
                                const int16_t *coef);
/* @0x1000f090 */
void TV_THISCALL Prosody_Build(Engine *self);
/* @0x10017900 */
uint8_t TV_THISCALL Tracks_Op(Engine *self, int32_t op, int32_t arg);

/* ---- stage windows (stage.c) --------------------------------------------- */

/* @0x10009020 */
uint8_t TV_THISCALL Engine_TypeSelected(Engine *self, Node *n);
/* @0x10008ca0 */
Node *TV_THISCALL Engine_StagePrev(Engine *self, Node *n);
/* @0x10008ea0 */
Node *TV_THISCALL Engine_StageNext(Engine *self, Node *n);
/* @0x10009050 */
uint8_t TV_THISCALL Engine_StageBegin(Engine *self, StageCtx *st);
/* @0x10008cd0 */
uint8_t TV_THISCALL Engine_StageEnd(Engine *self);

/* ---- control nodes (control.c) -------------------------------------------- */

/* @0x1000ead0 */
uint8_t TV_THISCALL Engine_RunControl(Engine *self);

/* ---- stage 0 helpers (stage0.c) ------------------------------------------ */

/* @0x10013310 */
void TV_THISCALL Stage0_Reset(Engine *self);
/* @0x100141d0 */
uint8_t TV_CDECL Stage0_CharClass(int32_t cls, uint8_t c);
/* @0x10014130 */
uint8_t TV_THISCALL Stage0_MatchWord(Engine *self, uint8_t list, uint8_t fold_case);
/* @0x10014010 */
uint8_t TV_THISCALL Stage0_Finish(Engine *self, uint8_t done);
/* Appends one node to stage 0's output, in front of the control node while
 * there is one and on the end of the work list once there is not. */
/* @0x10014090 */
void TV_THISCALL Stage0_Emit(Engine *self, int32_t type, uint8_t value,
                             uint8_t mark);
/* Whether a character is one the rules can name: not C, F, I, N or x. */
/* @0x10014290 */
uint8_t TV_CDECL Stage0_IsPlain(uint8_t c);
/* Puts the stress flag on one vowel of stage 0's window. */
/* @0x10014310 */
void TV_THISCALL Stage0_Stress(Engine *self, uint8_t adverb);
/* The nearest node behind this one whose value is A E I O or U. */
/* @0x10014720 */
Node *TV_THISCALL Stage0_PrevVowel(Engine *self, Node *n);

/* ---- stage 4 and the parameter tracks (stage4.c) ------------------------- */

/* @0x10004810 */
void TV_THISCALL Stage4_Reset(Engine *self);
/* @0x10004820 */
int32_t TV_THISCALL Stage4_Run(Engine *self);
/* @0x10017d90 */
void TV_STDCALL Track_Fill(uint8_t *buf, int32_t pos, int32_t n, uint8_t value);
/* @0x1000b060 */
void TV_THISCALL Track_BlendFwd(Engine *self, uint8_t *buf, int32_t pos,
                                int32_t shape, int32_t n, uint8_t target);
/* @0x1000afe0 */
void TV_THISCALL Track_BlendBack(Engine *self, uint8_t *buf, int32_t pos,
                                 int32_t shape, int32_t n, uint8_t target);

/* ---- the parameter setters and the diagnostics (params.c) ---------------- */

/* @0x10008a40 */
void TV_THISCALL Engine_SetPitch(Engine *self, int32_t pitch);
/* @0x10008a70 */
void TV_THISCALL Engine_SetSpeed(Engine *self, int32_t wpm);
/* @0x10008aa0 */
void TV_THISCALL Engine_SetVolume(Engine *self, uint32_t vol);
/* @0x10008b10 */
void TV_THISCALL Engine_SetVoice(Engine *self, uint32_t voice);
/* Both stubbed out in the shipping build; see the note in params.c. */
/* @0x10008b40 */
void TV_CDECL Engine_Trace(void *obj, const char *fmt, ...);
/* @0x10008b50 */
void TV_THISCALL Engine_Error(Engine *self, int32_t code);

/* ---- the rule interpreter's helpers (rule.c) ----------------------------- */

/* @0x10021630 */
int32_t TV_STDCALL Rule_TestBits(const uint32_t *want, Token *t, int32_t any);
/* @0x10021600 */
int32_t TV_THISCALL Rule_SetTrail(TextIn *self, Token *t, uint32_t v);
/* @0x100215d0 */
int32_t TV_THISCALL Rule_MatchTrail(TextIn *self, Token *t);
/* digits must be writable: above three digits the formatter truncates it in
 * place as it recurses and puts it back before returning.  See rule.c. */
/* @0x10021be0 */
int32_t TV_THISCALL Rule_SayGroupedNumber(TextIn *self, Token **first,
                                          uint32_t v, int32_t force_space);
/* @0x100212b0 */
int32_t TV_THISCALL Rule_SayRecord(TextIn *self, Token *t,
                                   uint32_t key, int32_t plural);
/* @0x10020f00 */
int32_t TV_CDECL Word_IsAcronym(const char *s);
/* @0x10020d70 */
int32_t TV_THISCALL Rule_Acronym(TextIn *self, Token *t, uint32_t v);
/* Returns the value it parsed, which Rule_SayGroupedNumber stores back
 * into Token.num; every other caller ignores it. */
/* @0x10020920 */
int32_t TV_CDECL Number_Words(char *digits, char *out,
                              int32_t style, int32_t mode);
/* and the six-argument formatter behind it, which recurses on itself */
/* @0x100200d0 */
int32_t TV_CDECL Number_WordsEx(char *digits, char *out,
                                int32_t style, int32_t mode, int32_t a, int32_t b);
/* @0x10021720 */
int32_t TV_THISCALL Rule_SayNumber(TextIn *self, Token *t, uint32_t v);
/* @0x10021820 */
int32_t TV_THISCALL Rule_SayNumberText(TextIn *self, Token *t, uint32_t v);
/* @0x10021aa0 */
int32_t TV_THISCALL Rule_SayNumberOrSpell(TextIn *self, Token *t, uint32_t v);
/* @0x10022810 */
int32_t TV_THISCALL Rule_SpellOut(TextIn *self, Token *t,
                                  uint32_t v, int32_t all);
/* @0x10021670 */
int32_t TV_THISCALL Rule_InsertWord(TextIn *self, Token *ref,
                                    uint32_t v, int32_t dir);
/* @0x10021080 */
int32_t TV_THISCALL Rule_Scan(TextIn *self, const uint32_t *want, Token *t,
                              int32_t dir, int32_t count, int32_t check);
/* @0x1001d9f0 */
Token *TV_THISCALL TextIn_Detach(TextIn *self, Token *t);
/* @0x1001dce0 */
int32_t TV_THISCALL TextIn_Error(TextIn *self, int32_t code);

/* ---- the rule interpreter (interp.c) ------------------------------------- */

/* @0x1001e5d0 */
int32_t TV_THISCALL Rule_Eval(TextIn *self, Token **cursor, Token **anchor,
                              uint32_t v);

/* The eight handlers the corpus never reaches, still bound to the DLL.  They
 * are named for the opcode that calls them, which is the only thing
 * established about them; nothing here claims to know what they do. */
/* @0x100211b0 */
int32_t TV_THISCALL Rule_Op60(TextIn *self, const uint32_t *want, Token *t,
                              int32_t dir);
/* @0x10021470 */
int32_t TV_THISCALL Rule_Op64(TextIn *self, Token *t, uint32_t v, int32_t n,
                              int32_t flag);
/* @0x100221f0 */
int32_t TV_THISCALL Rule_Op77(TextIn *self, Token *t, uint32_t v);
/* @0x10020a50 */
int32_t TV_THISCALL Rule_Op78(TextIn *self, Token **t, uint32_t v);
/* @0x10021960 */
int32_t TV_THISCALL Rule_Op80(TextIn *self, Token *t, uint32_t v);
/* @0x10020950 */
int32_t TV_THISCALL Rule_Op82(TextIn *self, Token *t, uint32_t v);
/* @0x10021fb0 */
int32_t TV_THISCALL Rule_Op85(TextIn *self, Token *t, uint32_t v);

/* ---- the lexicon and abbreviation indexes (tables.c) --------------------- */

/* @0x1001dd10 */
int32_t TV_CDECL Abbrev_Init(void);
/* @0x10023120 */
int32_t TV_CDECL Lexicon_Init(void);
/* Binary-searches the loanword lexicon; returns how many entries match and
 * puts the first matching index in *first. */
/* @0x10023160 */
int32_t TV_CDECL Lexicon_Find(const char *word, int32_t *first);
/* The same search over the abbreviation table. */
/* @0x1001dd50 */
int32_t TV_CDECL Abbrev_Find(const char *word, int32_t *first);
/* Looks the token up in the abbreviation table and, when one of the records
 * fits, copies it onto the token. */
/* @0x1001de40 */
int32_t TV_CDECL Rule_MatchAbbrev(Token *t);
/* Flags a numeric token as a plain number, an ordinal, a day, a month, a year,
 * an hour or minutes, from what follows it and what came before. */
/* @0x1001fde0 */
int32_t TV_STDCALL Rule_ClassifyNumber(Token *t);
/* Counts what the token's characters are and turns the counts into flags: all
 * capitals, all punctuation, a Roman numeral, a dotted abbreviation, a number.
 * Always returns 1. */
/* @0x1001f950 */
int32_t TV_THISCALL Rule_ClassifyToken(TextIn *self, Token *t);

/* ---- stage 2 and stage 3 helpers (stage2.c, stage3.c) -------------------- */

/* @0x1001aa60 */
uint8_t TV_THISCALL Stage2_Scan(Engine *self, int32_t dir, int32_t count,
                                int32_t mask1, int32_t mask2, int32_t mode);
/* Runs the control nodes at the head of stage 2's window, stopping at the
 * first phoneme. */
/* @0x1001a7e0 */
void TV_THISCALL Stage2_RunControls(Engine *self);
/* Inserts the pause a punctuation mark is worth. */
/* @0x10019eb0 */
void TV_THISCALL Stage2_Punctuation(Engine *self);
/* One word boundary: the two neighbour searches, then the class, the level
 * and the duration. */
/* @0x10019b10 */
void TV_THISCALL Stage2_Boundary(Engine *self);
/* The phoneme substitutions: N to ~, the stops lowered, and the fourteen
 * diphthongs merged into one node each. */
/* @0x10018270 */
void TV_THISCALL Stage2_Substitute(Engine *self);
/* Works out how long the phoneme lasts and writes it to the control node. */
/* @0x10018aa0 */
void TV_THISCALL Stage2_Duration(Engine *self);
/* Merges the boundary node behind the control node into it. */
/* @0x10018a10 */
void TV_THISCALL Stage2_MergeBack(Engine *self);
/* Walks to the end of the word, inserting the pauses and marks it finds
 * reason for; returns the node the caller should treat as the last. */
/* @0x1001a380 */
Node *TV_THISCALL Stage2_ScanWord(Engine *self);
/* Moves the window on by a node or a word; returns whether the cursor
 * moved. */
/* @0x1001a220 */
uint8_t TV_THISCALL Stage2_Advance(Engine *self);
/* One word's phonemes, folding their stress marks in.  Returns whether the
 * word is finished with. */
/* @0x1001a6d0 */
uint8_t TV_THISCALL Stage2_Word(Engine *self);
/* Works out the phoneme's level and writes it to the control node's b15. */
/* @0x10019710 */
void TV_THISCALL Stage2_Contour(Engine *self);
/* Two words meeting on the same phoneme: drops one.  Returns whether it did. */
/* @0x10019d10 */
uint8_t TV_THISCALL Stage2_Elide(Engine *self);
/* Works out what kind of word boundary this is, into s2_3c8, s2_3c9 and
 * s2_3d0. */
/* @0x100195c0 */
void TV_THISCALL Stage2_Classify(Engine *self);
/* Latches the control node's length and level, or writes them back. */
/* @0x1001ab90 */
void TV_THISCALL Stage2_Adjust(Engine *self, int32_t apply);
/* Caches the control node's class bits and its three neighbours. */
/* @0x10019bd0 */
void TV_THISCALL Stage2_Cache(Engine *self);
/* Counts the vowels ahead of or behind a node, treating two together as one. */
/* @0x10019490 */
int32_t TV_THISCALL Stage2_CountVowels(Engine *self, Node *from,
                                       uint8_t forward);
/* Clears the state stage 2 keeps for one utterance. */
/* @0x1001a9e0 */
void TV_THISCALL Stage2_ClearRun(Engine *self);
/* Sets flag 0x20 on a node and on up to three more behind it. */
/* @0x10019a00 */
void TV_THISCALL Stage2_MarkBack(Engine *self, Node *n);
/* Folds a '1', '2' or '\"' stress mark into the vowel before it. */
/* @0x10019a80 */
Node *TV_THISCALL Stage2_ApplyStress(Engine *self, Node *n);
/* @0x1001b810 */
void TV_THISCALL Stage3_Reset(Engine *self);
/* One phoneme's worth of stage 3: every correction pass in turn, then the
 * 22 tracks.  Stage3_Run calls it once per phoneme. */
/* @0x1001b880 */
void TV_THISCALL Stage3_Build(Engine *self);
/* @0x1001be30 */
Node *TV_THISCALL Stage3_Insert(Engine *self, Node *ref, int32_t mode);
/* One pause's worth of samples: builds a 22-byte parameter frame from
 * `mode` and writes it into every track buffer until `count` or the
 * buffers' `room` runs out, returning what is still owed. */
/* @0x1001b250 */
int32_t TV_THISCALL Stage3_FillFrames(Engine *self, uint8_t mode,
                                     int32_t count, int32_t room,
                                     uint8_t rebuild);
/* Advances every track by the duration it was given and republishes
 * trk_0c and trk_10 as the earliest and latest cursor. */
/* @0x1001c040 */
void TV_THISCALL Track_Commit(Engine *self);
/* One call's worth of a pause: takes the length off the node, then hands
 * the tracks as much of it as they have room for, and returns what is still
 * owed. */
/* @0x1001b170 */
int32_t TV_THISCALL Stage3_Fill(Engine *self);
/* Looks ahead for what ends the current run and inserts the pause that goes
 * with it; returns the node stage 3 should scan from next. */
/* @0x1001c110 */
Node *TV_THISCALL Stage3_Pause(Engine *self);

/* A sparse-record index over a bitmap: BitTable_Rank gives the packed
 * position of the record at (a, b) in table `kind`, or -1 when there is
 * none, and BitTable_Count gives the set bits in the first `nbytes` of a
 * row -- which callers use as the count of records.  See lang/spa/engine/bittab.c on
 * what the arguments are not yet known to mean. */
/* @0x100122f0 */
int32_t TV_STDCALL BitTable_Count(int32_t kind, int32_t nbytes);
/* @0x10012220 */
int32_t TV_STDCALL BitTable_Rank(int32_t a, int32_t b, int32_t kind);
/* @0x10012df0 */
int32_t TV_THISCALL Variant_Find(Engine *self, int32_t rec, int32_t vowel,
                                 int32_t ctx0, int32_t ctx1, int32_t nctx);
/* Selects the record for the consonant pair around the vowel that
 * stage 3's scan node sits on, and sets four track parameters from
 * it.  Returns 1 when it set them and 0 when it found nothing. */
/* @0x10012f30 */
uint8_t TV_THISCALL Cluster_SetTracks(Engine *self, int32_t vowel);
/* Writes a straight line from `from` to `to` into `n` bytes of a track
 * buffer, starting at `start` and wrapping at 256. */
/* @0x10012ed0 */
void TV_STDCALL Ramp_Fill(uint8_t *buf, int32_t start, int32_t n,
                          int32_t from, int32_t to);
/* Sets track `track` (9..12) and its partner at track + 4. */
/* @0x10012cb0 */
void TV_THISCALL Track_Set(Engine *self, int32_t a, int32_t b, int32_t c,
                           int32_t unused4, int32_t track,
                           int32_t unused6);
/* Glides a track from `to` back to `from` over `tcon` samples and holds
 * at `from` for the rest of the `n` bytes.  Arith.c's Extend(). */
/* @0x1000af20 */
void TV_THISCALL Extend(Engine *self, uint8_t *buf, int32_t start,
                        int32_t tcon, int32_t n, int32_t to,
                        int32_t from);
/* Lays a whole parameter contour -- six levels, four breakpoints -- across
 * one segment of a track, and a three-run contour across its partner. */
/* @0x10012390 */
void TV_THISCALL Track_Contour(Engine *self,
                               int32_t l1, int32_t l2, int32_t l3,
                               int32_t l4, int32_t t0, int32_t t1,
                               int32_t t2, int32_t t3, int32_t p2,
                               int32_t l5, int32_t p3, int32_t l0,
                               int32_t p1, int32_t track,
                               int32_t unused15);
/* Builds the whole parameter set for one phoneme segment: finds the
 * record for the consonant pair, unpacks it as a bit stream and drives
 * Track_Contour, Track_Set and Ramp_Fill over four tracks. */
/* @0x100117e0 */
uint8_t TV_THISCALL Segment_Apply(Engine *self, int32_t vowel);
/* The per-phoneme corrections to the track parameters: two dozen tests on
 * stage 3's three phonemes, each nudging particular trk_param cells. */
/* @0x10010ba0 */
void TV_THISCALL Track_Adjust(Engine *self);
/* Emits one track's pending shape into its buffer, choosing among the two
 * blends and Extend by the mode in trk_param[track][0]. */
/* @0x10017aa0 */
void TV_THISCALL Track_Emit(Engine *self, int32_t track);
/* One phoneme segment of stage 3: Segment_Apply when the control node is a
 * vowel, otherwise the consonant corrections and a pass over tracks 9 to
 * 11 that emits each and then restores its parameters. */
/* @0x10015720 */
void TV_THISCALL Stage3_Segment(Engine *self);
/* Three more of sub_1001b880's correction passes; see lang/spa/engine/adjust.c on why
 * the grouping is the original's rather than a derived one. */
/* @0x10014f20 */
void TV_THISCALL Track_AdjustWeights(Engine *self);
/* @0x10016220 */
void TV_THISCALL Track_AdjustBeforeR(Engine *self);
/* @0x100162b0 */
void TV_THISCALL Track_AdjustGap(Engine *self);
/* Pulls tracks 10 and 11 toward each other over a run of samples. */
/* @0x100179d0 */
void TV_THISCALL Track_Couple(Engine *self, int32_t pos, int32_t count,
                              int32_t gain);
/* Sets the emit mode on every track and the blend shape on 9 to 16. */
/* @0x10016350 */
void TV_THISCALL Track_SetModes(Engine *self);
/* The trill/tap adjacency cases. */
/* @0x10014fb0 */
void TV_THISCALL Track_AdjustTrill(Engine *self);
/* Track_SetModes' sibling, keyed on the current phoneme. */
/* @0x10015410 */
void TV_THISCALL Track_SetShapes(Engine *self);
/* Blends a track backward by a fixed delta rather than toward a target. */
/* @0x10017dc0 */
void TV_THISCALL Track_BlendDelta(Engine *self, uint8_t *buf, int32_t mode,
                                  int32_t pos, int32_t tcon, int32_t n,
                                  int32_t delta);
/* Winds tracks 0 and 2 back by s3_458 and redraws the vacated run. */
/* @0x10017e60 */
void TV_THISCALL Track_Shorten(Engine *self);
/* @0x10015090 */
void TV_THISCALL Track_AdjustPause(Engine *self);
/* @0x10010a30 */
void TV_THISCALL Track_AdjustVelar(Engine *self);
/* Averages each track's old level with its new one, within a limit. */
/* @0x10011660 */
void TV_THISCALL Track_Average(Engine *self);
/* Emits a run of fixed values into tracks 0, 2 and 13 and accounts for it. */
/* @0x10017f90 */
void TV_THISCALL Track_EmitPause(Engine *self);
/* The phoneme boundary: whether to emit a pause, and what follows it. */
/* @0x10015540 */
void TV_THISCALL Track_AdjustBoundary(Engine *self);
/* Writes a stop's release burst back into already-laid audio. */
/* @0x100150f0 */
void TV_THISCALL Track_StopBurst(Engine *self);
/* Loads a phoneme's voice parameters into the whole track set. */
/* @0x1001b440 */
void TV_THISCALL Stage3_LoadPhone(Engine *self);
/* Which of stage 3's two phonemes are flagged, as two bits. */
/* @0x10017550 */
int32_t TV_THISCALL Stage3_BlendMode(Engine *self);
/* Sets tracks 9 to 11 from one of two triples, or their average. */
/* @0x10017630 */
void TV_THISCALL Track_SetTriple(Engine *self, int32_t a1, int32_t a2,
                                 int32_t a3, int32_t b1, int32_t b2,
                                 int32_t b3, int32_t mode);
/* Puts one or both phonemes into a vowel class, 1 to 5. */
/* @0x100176c0 */
void TV_THISCALL Stage3_VowelClass(Engine *self, int32_t mode);
/* Corrections for the transition between two phonemes, either direction. */
/* @0x10010630 */
void TV_THISCALL Track_AdjustTransition(Engine *self);
/* Loads the two level triples for a transition from the per-vowel-class
 * tables and hands them to Track_SetTriple. */
/* @0x10016fe0 */
void TV_THISCALL Stage3_LoadTriples(Engine *self);
/* Stage3_LoadTriples' sibling: fills both triples when both phonemes are
 * flagged, and lets Track_SetTriple average them. */
/* @0x10015b30 */
void TV_THISCALL Stage3_BlendTriples(Engine *self);
/* A stop closure, and then the triples for whatever follows it. */
/* @0x10016460 */
void TV_THISCALL Stage3_StopClosure(Engine *self);

/* ---- not written yet ------------------------------------------------------
 * Empty, and that is the point: every function the corpus executes is written,
 * so nothing is left that needs declaring with an address and no definition.
 * tools/gen_hookmap.py emits a --defsym for every annotated symbol it cannot
 * find a definition of, so a call to one of those lands in the original inside
 * the loaded DLL, and that is what made it possible to decompile one function
 * at a time rather than a whole subsystem at once.  Three such declarations are
 * left in the tree, in lang/spa/engine/textin.c: the mode 4 header handlers, which nothing
 * can reach yet. */


/*
 * OpenTV: voices of our own, past the ten the DLL carries.  The mechanism is
 * the English engine's mirrored for this one; see lang/spa/engine/voices.c.
 */
#define ES_STOCK_VOICES 10
#define ES_V_INHERIT    (-0x7fffffff)

typedef struct {
    const char *name;
    int32_t     base;           /* the stock voice anything unset comes from */
    int32_t     adjust[15];     /* the fifteen-wide row, or ES_V_INHERIT */
    int32_t     pitch, speed, rate_index;
    int32_t     c8a0;
    int32_t     p18, p19, p20, p21;
    int32_t     c7c0, voice_c;
    int32_t     gain;           /* OpenTV's own: source level, per cent */
    int32_t     inton;          /* OpenTV's own: contour depth, per cent */
    int32_t     aspir;          /* OpenTV's own: breathiness floor */
} EsVoiceDef;

extern const int32_t es_extra_voice_count;
const char *es_extra_voice_name(int32_t v);
const int32_t *es_v_adjust(int32_t v);
int32_t es_v_gain(int32_t voice, int32_t v);
int32_t es_v_aspir(int32_t voice);
int32_t es_v_inton(int32_t voice);
int32_t es_v_pitch(int32_t v);
int32_t es_v_rate_index(int32_t v);
int32_t es_v_speed(int32_t v);
int32_t es_v_c8a0(int32_t v);
int32_t es_v_p18(int32_t v);
int32_t es_v_p19(int32_t v);
int32_t es_v_p20(int32_t v);
int32_t es_v_p21(int32_t v);
int32_t es_v_c7c0(int32_t v);
int32_t es_v_voice_c(int32_t v);

#endif /* TV_ES_ENGINE_H */
