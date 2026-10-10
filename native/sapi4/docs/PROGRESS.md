# Decompilation progress

Target: Microsoft's SAPI 4 text-to-speech engine `msttssyn.dll` ("Speech Build 4.0.4.2512", 1999-01-12,
851,456 bytes, SHA-256 a8a2ddb9...f731), the engine behind Microsoft Sam, Mike, Mary and their 19 voice
modes. Its PDB path is `P:\whistler\ship\msttssyn5\WinRel\msttssyn.pdb` and it names `.\libsrc\cart.cpp`:
this is Microsoft Research's Whistler synthesizer, the ancestor of the SAPI 5 engine (`spttseng.dll`)
that KamiKitsune420/ms-sam-mike-mary-decomp reconstructs (see "Lineage" below).

Verification: every decompiled function is hooked over the original inside the emulator (the original
DLL keeps running around it) and must leave the output bit-identical:

* `make check`: the 5 genuine-engine references from tetyys.com (Sam x2, Mike, Mary, RoboSoft One).
* `make golden`: 46 renders over all 19 modes, two texts, speed/pitch extremes.
* `make unit`: every decompiled function against the original, called directly in the emulator on
  2,000 random inputs each (all output buffers, the return value, x87 results as exact doubles). This
  covers paths the speech corpus never takes (8-bit output, filters of unused modes, odd sizes, ties).

All three pass with every hook enabled. `SAPI4_HOOKS=none` gives the untouched original.

The portable build (no emulator) is checked against the same originals: `make port-render-check`
(all 46 golden renders, bit for bit, speed and pitch included), `make port-tags-check` (84 tagged
renders against the original), `make userlex-check` (user lexicon files), `make port-fe-check` (phone
lists), `make port-lex-check` (the main lexicon); `make port-render-full-check` runs the fully baked
binary (DLL data and voice files compiled in) with no voice directory at all.

The library (lib/sapi4tts.h) is checked through its API: `make api-check` (the 46 golden renders in one
process with a fresh engine each, again on four threads; the 84 tagged renders; the 22 user-lexicon
renders; a warm engine's second utterance; every mode's pitch and speed limits; stop, reset, clamping
and the engine limit), `make api-baked-check` (the golden set with the DLL's data and the voices
compiled into the library), `make ios-check` (the library compiled for arm64 iOS, a test program
linked against it).

## Where this stands (end of session 7, 2026-09-27)

Measured by `tools/progress.py` (static list from `tools/funcs.py`, reachability from `tools/cover.sh`):

| | functions | bytes |
|---|---|---|
| static function list (excl. 73 import stubs; approximate) | ~1,000 | ~185,000 of .text 184,302 (boundaries approximate) |
| reached by the corpus | 536 | 145,161 |
| **decompiled and verified** | **522** (445 reached, 77 reached only by the unit tests or the new checks) | **145,966** |
| share of the reached set | 83.0% | 92.8% |
| share of all guest instructions executed | | 100.0% (rounded) |

**Session 7: the library.** `lib/sapi4tts.h` is the embedding API: `s4_init` (the DLL's data from a
file, or compiled in), `s4_list_modes`, `s4_open` (a mode by name, voices from a directory or compiled
in), `s4_speak` (PCM to a callback, streamed unit by unit), pitch and speed get/set with SAPI's
clamping, `s4_stop` (any thread, never blocks), `s4_reset`, `s4_close`. `build/libsapi4tts.a` for
arm64 macOS, `build/ios/libsapi4tts.a` for arm64 iOS, `build/libsapi4tts_baked.a` with everything
compiled in. No functions were added to the decompilation this session; the engine sequence the
library runs is the one the golden checks have verified since session 5.

**Session 6: the portable renderer is feature-complete for the SAPI text path.** Speed and pitch are
set as a SAPI client sets them, and all 46 golden renders come out identical with no emulator. Tagged
text is covered too (84 renders of every SAPI 4 tag, well and badly formed, identical to the original).
There is a fully baked single binary (the DLL's data and the voice files compiled in, 9.6 MB), and the
per-user lexicon file is read. 23 functions (6.7 KB): the pitch and rate getters, the word module's
alternative prosody (mode 0, 12 functions; no shipped voice opens it, so it is verified by a unit test
on random word tapes) and the user lexicon reader (9 functions). See "Done, session 6".

Session 5 added 200 functions (63.8 KB). First part: **the whole front end except one unused prosody
mode** - the text input (wide text to single-byte, SAPI tags into the engine's escape sequences, line
breaks, sentence splitting with its lexer), the word module's reader (escapes into events, user
lexicon, phrase breaks, setjmp/longjmp error unwinding), machine A's per-phrase driver (durations,
segments, pitch), the phrase output, the front-end thread and its set-up; `make port-fe-check` ran
text to phone lists with the C alone. Second part: **voice loading and the engine's set-up** - the
voice file's streams for the unit stage (phone set, senone trees, statistics, alternative-unit rules)
and for the synthesizer (the Inventory: codebooks, unit blobs, the noise / sine / fade tables), the
synthesizer's mode characters (whisper, filters, room and RoboSoft effects), the enumeration of voices
and characters with their SAPI mode descriptions and the shared voice table, the constructors of the
queues and stage objects, the alternative-unit search's phrase rewrite; and the portable renderer.
See "Done, session 5".

Session 4 added 129 hooked functions (44.5 KB): the rest of the rule machine (rule execution, rule
match, the rule-set main loop, rule apply: 3 instances each), every rule procedure the three machines
call (87 table entries: 60 wrappers that run another rule set, 27 with work of their own), the
lexicon (the ten word dictionaries and the Huffman-coded trie of the main lexicon), the word module's
duration increments, and the first piece of the portable build: `make port-lex-check` runs the main
lexicon as C alone on 27,100 words, identical to the original (see "Done, session 4").

Session 3 added 59 (12.7 KB): the thread plumbing of the unit stage and the synthesizer, most of the
alternative-unit search, and the rule machine's tape routines, condition interpreter and runner.

History of the earlier sessions:

Session 1 did the synthesizer's signal path (38 functions). Session 2 added 73:
* **Unit stage** (the thread between front end and synthesizer): SAPI tag lexer and parser (11),
  its containers (hash table, vectors: 11), the senone decision-tree lookup and phone fix-ups,
  per-phrase unit selection, the prosody driver and both contour-interpolation passes (b[] and c[]).
  What is left of it: the alternative-unit lattice search (0x63686614 / 0x63685c20 / 0x636882fc /
  0x63685d72, mostly unreached), one notification timing routine (0x63687e26) and the thread
  plumbing (queues, events, PostMessage), which is now hookable (see "Coroutine hooks").
* **The C-runtime equivalence layer**: `src/vcrt.c` holds MSVCRT's C-locale ctype, the sign-only
  string/memory compares, stricmp/strnicmp, CP1252 and USER32's character functions, lstrcmp(i)A's
  word sort and MultiByteToWideChar. The emulator's own shims (`emu/crt.c`, `emu/winapi.c`) now call
  these, so there is exactly one definition; the emulator stayed bit-exact on every check.
  `src/crt_vc.c` adds the VC qsort, rand, and malloc/realloc/free/Sleep, which the hook build routes
  to the guest heap / guest thread state.
* **Front end, from its leaves**: 21 small helpers (text span accessors etc.), and the first templates
  of its generated rule modules: the DLL has ~10 modules compiled from the same templates, so
  `tools/dupes.py` groups identical code and `tools/instances.py` lists each copy's globals; one C
  function then serves every copy (tape helpers x19, table binary search x5).

Verification, all with every hook enabled: `make check` 5/5, `make golden` 46/46, `make hookdiff`
171/171 renders identical to the original in PCM **and in the final guest heap state**, `make unit`
173k checks over 1,000-2,000 random cases per function (struct methods run original and C on the
same guest memory; allocating functions also compare the whole heap; negative controls done for the
new areas).

## Engine architecture (as understood so far)

Recon from exports, vtables, strings, and a dynamic call graph of the corpus (`tools/cover.sh`):

```
host (SAPI 4 client)
  DllGetClassObject -> class factory -> ITTSEnum (vtable 0x636a0178)
     Next     0x6367eaf3                 enumerate the 19 modes (reads *.cfg / *.vce headers)
     Select   0x6367ef50  (126 functions, 20 KB reached)  loads the voice (.vce: Cdb, Senone, AltUnits,
                                         VcHeader, Inventory, PhoneFile, TreeImage; .cfg: CFG1..7)
  ITTSCentral (vtable 0x6369ff40): TextData 0x6367bcde queues text; Register 0x6367c261; ...
  ITTSAttributes (0x636a0000): PitchSet 0x6367c6ce, SpeedSet 0x6367c7e7, ...
  three worker threads, started around 0x6367b0f9:
    0x636844b6  front end   308 functions, 94 KB reached, 6.7% of instructions:
                text normalisation (abbreviation table, numbers, dates, currency), lexicon and
                letter-to-sound (compressed pronunciations in .rdata), CART trees (cart.cpp),
                phrasing / prosody
    0x63687d3f  unit stage   55 functions, 12.5 KB, 0.5%: senone / unit selection from the voice's
                decision trees (TreeImage, PhoneFile), durations and F0 (sqrt, x87)
    0x636822a1  back end     57 functions, 10.5 KB, 91.4%: the synthesizer - a pitch-synchronous
                LPC vocoder: LSF frames -> LPC (0x63678470), excitation periods from the Inventory
                (0x63675c05 decodes them; inverse FFT 0x63684b60 builds pulses), all-pole synthesis
                filter (0x636712d5), the mode effects (reverb / RoboSoft / whisper: 0x63680189 and
                friends, a C++ object with a delay line), then float -> 16-bit PCM (0x63671ebc) and
                IAudioDest::DataSet through posted window messages
```

Voice data format: `.vce` is an OLE compound file. The `Inventory` stream is byte-for-byte the
synthesizer data the SAPI 5 engine keeps in section 3 of its `.spd` (codebooks, unit blobs; see
ms-sam-mike-mary-decomp `tools/vce2spd.py`).

## Lineage with the SAPI 5 engine (ms-sam-mike-mary-decomp)

Confirmed shared code, not just shared data: the SAPI 5 port's `ifft_split_radix` (`FUN_5ed58259`)
and `lsf_to_lpc` (`FUN_5ed57f9e`) are the same algorithms as msttssyn's 0x63684b60 and 0x63678470,
line for line in structure. Its `sam4fx.c` reconstructs this DLL's voice-mode effects (preset table
at 0x6371f018). Differences that matter for bit-exactness, found while decompiling:

* The SAPI 5 port is "within 1 LSB"; this project must be exact, so the x87 code decides: e.g. the
  FFT parks several intermediates in float stack slots (rounded to float) that the SAPI 5 port keeps
  in double, divides by n instead of multiplying by (float)(1/n), and LSF->LPC uses a double 2*pi
  (6.283185307179586) where the SAPI 5 engine uses the float 6.2831855.

So the SAPI 5 port is an excellent map of *what* each back-end routine does; the exact arithmetic
still has to come from this binary.

## Done, session 1: the signal path (38 functions)

| address | name | what | verified by |
|---|---|---|---|
| 0x636710da | voc_pitch_marks | pitch-mark placement from the pitch contour (interpolated), unvoiced stretches, reversed repeats | golden, check, hookdiff |
| 0x636712d5 | dsp_allpole | all-pole LPC synthesis filter (26% of instructions) | golden, check, unit |
| 0x6367131b | dsp_fit_period | fit an excitation period to a new length with a window cross-fade | golden, check, unit |
| 0x636713b8 | dsp_period | reverse / copy / fit a period, then gain | golden, check, unit |
| 0x63671429 | voc_excitation | assemble the excitation, one period per pitch mark, gain contour | golden, check, hookdiff |
| 0x636715e2 | voc_synth_unit | synthesize one unit request (the per-unit driver) | golden, check, hookdiff |
| 0x63671943 | dsp_fir | FIR filter in place (mode filter) | golden, check, unit |
| 0x636719a0 | dsp_iir_c0 | recursive mode filter (not reached by the corpus) | unit |
| 0x636719fa | voc_unit_decode | decode a unit: periods, LSF frames -> LPC, gains, excitation via codebook spectra + inverse FFT, or noise | golden, check, hookdiff |
| 0x63671e49 | dsp_sum_abs | sum of magnitudes (ST0) | golden, check, unit |
| 0x63671e79 | dsp_scale | gain | golden, check, unit |
| 0x63671e96 | dsp_reverse | reversed copy | golden, check, unit |
| 0x63671ebc | dsp_to_pcm16 | float -> 16-bit PCM (fistp, no clipping) | golden, check, unit |
| 0x63671eee | dsp_to_pcm8 | float -> unsigned 8-bit PCM (not reached) | unit |
| 0x63671f31 | dsp_max_abs | peak magnitude (ST0) | golden, check, unit |
| 0x63675bc4 | voc_unit_free | free a unit | golden, heap identical |
| 0x63675c05 | codec_bytes_to_floats | escape-coded byte stream -> floats; returns bytes consumed in EAX | golden, check, unit |
| 0x63678470 | lpc_from_lsf | LSF -> LPC (sorts the LSFs in place when needed) | golden, check, unit |
| 0x63684b60 | fft_inverse_real | Sorensen split-radix inverse real FFT (36%) | golden, check, unit |
| 0x6367fbfe | effect_db_to_gain | dB -> linear (ST0) | golden, unit |
| 0x6367fc36 | effect_ap_clear | clear a delay line | golden |
| 0x6367fc54 | effect_ap_init | set up an allpass stage | golden, heap |
| 0x6367fcb7 | effect_build_stages | allocate the preset's stages | golden, heap |
| 0x6367fd50 | effect_free | free stages and scratch (not reached) | unit (with heap snapshot) |
| 0x6367fda2 | effect_preset | mode 1..7 -> preset table in .data | golden, unit |
| 0x6367fdf5 | effect_init | set up the effect for a mode and rate | golden, unit (with heap snapshot) |
| 0x6367fee8 | effect_ctor | constructor | golden, unit |
| 0x6367ff1d | effect_gain_copy | send copy | golden, unit |
| 0x6367ff87 | effect_mix_clip | dry + wet, clipped to 16 bits | golden, unit |
| 0x63680036 | effect_allpass | one allpass delay line (optional FIR in the loop) | golden, unit |
| 0x63680155 | effect_chain | up to five allpasses | golden, unit |
| 0x63680189 | effect_process | the effect over a buffer, RoboSoft warble from rand() | golden, unit |
| 0x6368864b | dsp_fold_pulse | fold a pulse into a period (overlap-add of its halves) | golden, check, unit |
| 0x636886c6 | dsp_scale_b | a second copy of dsp_scale | golden, check, unit |
| 0x636886e3 | dsp_cmp_float | qsort comparator | unit |
| 0x63688708 | dsp_sort_floats | VC-qsort of floats, returns "was sorted" | golden, check, unit |
| 0x6368874c | dsp_place_halves | place a codebook entry's halves into the spectrum | golden, check, unit |
| 0x636887a7 | dsp_add_halves | add them (delta frames) | golden, check, unit |

## Done, session 2 (73 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x63680f65, 0x63681002, 0x6368104a, 0x636810a6, 0x636810de, 0x6368114d | tag_lex, tag_lex_number/word/string/brace, tag_keyword | SAPI tag lexer | golden, hookdiff (tags), unit (token stream + buffers) |
| 0x63681196, 0x6368119e, 0x636811d9, 0x63681237, 0x63681315 | tag_parse_*, tag_parse | tag parser (22-keyword table in .data) | golden, hookdiff, unit (with heap) |
| 0x63675e95, 0x6367fb1e, 0x6367fb43, 0x63685bb9 | hash_lookup, named_index, named_item, prefix_cmp | string hash table, name lookups | golden, hookdiff |
| 0x63688151..0x6368824c, 0x63685814 | intvec_*, vecarray_*, blob_copy80 | vectors, blobs | golden, hookdiff (heap) |
| 0x63674184 | senone_lookup | senone decision trees | golden, unit (random trees) |
| 0x636876b2 | phone_fixups | DX->T, unstressed diphthongs stressed | golden, unit |
| 0x63687751 | unit_select | phone index + senone per phone of a phrase | golden, hookdiff |
| 0x636871a4 | unit_prosody | durations, state values, output units | golden, hookdiff (heap) |
| 0x63686c7d | unit_prosody_a | b[] contour interpolation, state insertion | golden, unit (random phrases) |
| 0x63686905 | unit_prosody_b | c[] contour interpolation | golden, unit (random phrases, normalised by the original A) |
| 0x6369d3a0..0x6369d5a0 etc. (21) | span_*, pair_reset, block8_clear, free_if, is_digit_char, char_class16, freelist_push, ... | front-end helpers | golden, hookdiff |
| 0x63697558 + 8 copies | tapes_graphic_0..8 | rule modules: tape range all graphic | golden, unit (every copy) |
| 0x63697583 + 9 instances | tapes_pad_0..9 | rule modules: pad the four tapes | golden, unit (every instance) |
| 0x63697b42 + 4 instances | rule_bsearch_0..4 | rule modules: binary search in the module's table | golden, unit (every instance, real tables) |

`src/crt_vc.c` also carries the VC runtime's qsort algorithm (the order of equal elements shows in
the output) and `src/x87.h` the x87 conversions (fistp, _ftol, the |x| idiom).

## Done, session 3 (59 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| (IAT thunks) | harness/win32.c | EnterCriticalSection / WaitForMultipleObjects park the coroutine and retry; LeaveCS, SetEvent, ResetEvent, PostMessageA, SetThreadPriority call the emulator's own thunks; the wait logic is shared with the emulator (`emu_wait_poll`, `emu_cs_try_enter`) | hookdiff (heap) |
| 0x63684899, 0x63684905, 0x6368498f, 0x636849ab, 0x636849cd, 0x63684a29, 0x63684aa1, 0x6368447a, 0x6368225f, 0x63687cfd | queue_* (src/queue.c) | stage message queue: growth, high/low-water events, push/pop, locked getters | golden, hookdiff (heap) |
| 0x63687e26, 0x63687d4b, 0x63687de5, 0x63687fbb | unit_rate_update, unit_set_rate, unit_reset_rate, unit_set_amp | duration scale, final silences, notifications | golden, hookdiff, unit |
| 0x63687d3f, 0x63687ba4, 0x63687974, 0x6368571f | unit_thread_proc, unit_thread_loop, unit_phrase, unit_send | unit-stage thread, phrase driver, hand-off to the synthesizer (HOOKY) | golden, hookdiff (heap) |
| 0x63686614, 0x63685c20, 0x63685d18, 0x636882fc, 0x63685d00, 0x63685bf7, 0x636881b0, 0x636882d6 | unit_lattice, lattice_match, unit_span_lookup, lattice_viterbi, lattice_cost, array_lacks, intvec_append, vecarray_append | alternative-unit search: rule matching, span lookup, Viterbi over the lattice | golden, hookdiff, unit (synthetic lattices with a competing cost function, heap snapshots) |
| 0x636822a1, 0x63681ede, 0x63681cb2, 0x63681b7e, 0x63674d18, 0x6367ddfb | voc_thread_proc, voc_thread_loop, voc_synth_request, voc_send_audio, phone_table_index, identity32 | synthesis thread, request loop (unit to float samples, growth, clipping), audio hand-off with flow control | golden, check, hookdiff |
| 3 instances each (A 0x6368.., B 0x6369.., C 0x6369..; addresses in src/fe_vm.c) | vm_shift_right/left, vm_pop_range, vm_push_until, vm_skip_block, vm_move, vm_cond, vm_run, vm_try | the front end's rule machine: tape shifts, stack moves, bytecode block skip, condition interpreter, the rule runner with backtracking over choice points, trying one rule (27 hooks, one C function per routine) | golden, hookdiff, unit (random bytecode per instance; the original is aborted-safe) |

`tools/families.py` finds near-duplicate functions (same mnemonic sequence, different globals or
small edits) - that is how the three rule-machine instances were matched; instance A differs in
reading one position as int16 and in its apply guard.

## Done, session 4 (129 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x6368d966, 0x63691990, 0x63694ed7 | vm_match | the rule pattern matcher: literals, classes, variables, tape switches, bounded repetitions, embedded conditions | golden, hookdiff, unit (random patterns over small alphabets) |
| 0x6368f187, 0x636931b1, 0x636966f8 | vm_apply | a rule's output: a procedure (0xfe,n) or a replacement string, keeping per-character counts | golden, hookdiff, unit |
| 0x6368e53d, 0x63692568, 0x63695aaf | vm_exec | one context opcode: tests, filler skips, alternatives and optional parts (choice points), repetitions | golden, hookdiff, unit (every opcode on random state) |
| 0x6368c997, 0x636909b7, 0x63693ef8 | vm_main | a rule set over a segment: 16 scan strategies (forward / backward, anchored or not, all or first, single rules or groups); its frame lives on the guest stack (`vc_stack_push`) because procedures of the original get pointers into it | golden, hookdiff, unit (real rule sets on random segments; the rule types the data never uses, 9-12 and 15, by patching types in the emulator's .rdata) |
| 60 addresses (fe_procs.c table) | vm_proc (PROC_MAIN) | the rule procedures that run another rule set | unit (.data, tapes and heap compared) |
| 0x6368b28b, b2e0, b305, b383 (+ b468, b526), b6bc; 0x6369048f (+ 0x636905f9), 0x63690633, 0x63690215, 0x6369024b; 0x636939b0, 3984, 38b9, 38ef, 3925, 382c, 3a03, 397f | vm_proc (tape procedures) | prosody-tag numbers into the word object, quotes, dash numbering, pause placement, class marks, "x(y)" alternatives | golden, hookdiff, unit (tapes drawn from the symbols each one tests) |
| 0x6369a9c8, 0x6369a87e + 7, 0x6369a47b | lex_pack, lex_field, lex_field18 | letter-code key packing, packed n-bit field arrays | unit |
| 0x63698968, 0x63699322, 0x63698e44, 0x63697675, 0x63697e71 | lex_bsearch8w, lex_bsearch16, tapes_printable | the dictionaries' other searches | golden, hookdiff |
| 0x6369a4ea, 0x6369773d, 0x636980bf, 0x63698a3e, 0x63698594, 0x63698f1a, 0x636993f8, 0x63697c17, 0x636972c7 | lex_lookup | nine dictionaries, one function, the variants as data (`lex_dicts`) | golden, hookdiff, unit (half the words unpacked from the dictionaries' own keys) |
| 0x6369986b, 0x63699bd5, 0x6369a15f, 0x63699cd6, 0x63699d94, 0x63699e19, 0x63699e9e, 0x63699f23, 0x63699fae, 0x6369a0c9 | lex_main, lex_trie_* , lex_huff_* | the main lexicon: Huffman-coded trie walk, entry number, pronunciation with back-references | golden, hookdiff, unit (15,418 words of /usr/share/dict/words the lexicon has), port-lex-check |
| 0x63696dd8, 0x63696f5f, 0x6369713a, 0x63697261 | word_durations, word_cluster, word_cluster_find, word_stop_weight | letter-cluster codes and per-position duration increments of the word object | golden, hookdiff, unit |

Negative controls were run for each group (one changed comparison or constant per run); where a
change was not detected it was confirmed to be unobservable (e.g. an equality the data never
produces) or a generator was sharpened until it was.

## Done, session 5 (200 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x6368c49b, 0x6368bab2, 0x6368f732, 0x6368fed5, 0x6368c6f1, 0x6368c3dc, 0x6368b8e1, 0x6368b7b1, 0x6368b87c, 0x6368c177, 0x6368c1e3, 0x6368c367, 0x63690187 | ph_run, ph_durations, ph_pitch, ph_target, ph_segments, ... | machine A's per-phrase driver: rule set runs, duration and accent heuristics, segments, pitch targets | golden, hookdiff, unit (random word objects; the original divides by zero on some, those are skipped) |
| 0x6368aca2, 0x6368abe3, 0x6368af3e, 0x6368ac2f.. | ev_* | the phrase event list (nodes recycled through a free list) | golden, hookdiff, unit |
| 0x63696cd4, 0x6369378d, 0x6368b221, 0x63688d0a, 0x63688d9c, 0x6368b17a, 0x63688964, 0x6368b198, 0x6368899b, 0x63696dd8.. | fe_c_run, fe_b_run, fe_word_* | the word module's entry points and the output string for the phrase | golden, hookdiff |
| 0x636822e4 .. 0x6368337f (14) | txt_* | text rewriting before tokenising: emoticons, addresses, numbers, "approximately", characters outside ASCII | unit (texts built from those cases), golden |
| 0x6369d620, 0x6369c505 .. 0x6369c9e6 (9) | lex_next, lex_* | the lexer: token DFA, abbreviations, titles, common words, emoticons | unit (token streams; the lexer's own word tables) |
| 0x6369aaa4 and 15 helpers | split_sentences, quotes_*, brackets_*, word_* | the sentence splitter (5.4 KB state machine): brackets, quotes, initials, roman numerals, abbreviations | unit (generated texts, several sentences per call, a 51-deep bracket overflow), golden |
| 0x6367f5ef, 0x6367f53b | text_sentences, text_prepare | the text cut into sentences, line breaks | unit, golden |
| 0x6368043f, 0x636840f3, 0x63680bb7, 0x636803e4, 0x63680244, 0x6368030a, 0x63680f05, 0x63680f2d, 0x636812b6, 0x636812da, 0x636748d1, 0x63674978, 0x6367499a | fe_tags_to_escapes, fe_text_input, ... | SAPI tags into escape sequences with the numbered entry table; the text entry point | unit (every tag keyword, well and badly formed), golden, hookdiff |
| 0x63689357 (+ 0x63689726, 0x63689fee, 0x63689fc4), 0x6368aa05, 0x63689bc0, 0x63689fad, 0x636896e3, 0x63688b27, 0x63677575, 0x6368ac44 .. 0x6368aee5, 0x63688fb3, 0x636890e2 | word_run, rd_*, ev_add_* | the word module's reader: escapes into events and settings, words onto the tape, phrase breaks by length; errors unwind with longjmp (kept in C: the three functions that can longjmp are verified through their caller) | unit (texts of words and escapes, a user lexicon; .data, the word object and its events compared), golden, hookdiff |
| 0x63676ec7, 0x6367584b, 0x636758d1, 0x63675ffc, 0x63675fb5 | user_lex_* , phones_to_codes | user lexicon lookup and phone-name conversion | unit, golden |
| 0x63680c6e, 0x63680c3f, 0x636779a5, 0x63677af9, 0x63680ecd, 0x63683653, 0x63683765, 0x6367de02, 0x6367de34 | fe_after_feed, ... | after each part read: index escapes back into tags for the back end, the end-of-text message, pitch | golden, hookdiff |
| 0x6367764f, 0x636761ab, 0x63677b56, 0x636765ea, 0x636765a5, 0x6367678b, 0x63675f6b, 0x63676130, 0x63676159, 0x63677da6, 0x636778a7, 0x63677911, 0x6368021c, 0x63674478 | words_phrase, phone_list, ... | the phrase output: tapes copied out, phones with durations and interpolated pitch, the PhoneIn list | golden, hookdiff, port-fe-check |
| 0x636837a4, 0x636836b8, 0x63684233 | fe_phrase_out, fe_set_pitch_level, fe_thread | the phone list with tag records and the "\Tp" converter path; the front-end thread | golden, hookdiff, port-fe-check |
| 0x63677045, 0x63688803, 0x63689051, 0x636888e1, 0x63688a30, 0x63688bc2, 0x6368b129, 0x6368abc1, 0x636750ea, 0x6367518a, 0x6369aa68, 0x6367f50a, 0x636834d4, 0x636835bc, 0x636803b5, ... | words_open, word_obj_*, fe_ctor, fe_setup | set-up: word objects, user lexicons, the per-voice input buffers, the front end object | golden, hookdiff, port-fe-check |

| 0x6367f73d, 0x6367fab1, 0x6367fbb1, 0x63673dc2, 0x63673312 / 0x636732fa, 0x63685847, 0x6368587f, 0x63685b25, 0x636856db, 0x63685471 and helpers | phoneset_load, senone_header_load, senone_tree_load, alt_*, unit_init | the unit stage's voice data: phone set (PhoneFile), senone trees (TreeImage), per-senone statistics (Senone), alternative-unit rules (AltUnits); storage access as one C interface over the emulator's COM objects or src/cfb.c | golden, hookdiff (PCM and heap), port-render-check |
| 0x63672010, 0x63671f8d, 0x636720f4, 0x63675d45, 0x63675c95, 0x636850c0, 0x63685172, 0x63671063, 0x63681688, 0x636819e6, 0x6367fe99 | voc_header_load, voc_codebook_load, voc_inventory_load, voc_noise_make, voc_gauss, voc_sine_table, voc_fade_window, voc_setup, voc_engine_init, voc_init, effect_set_fir | the synthesizer's voice (Inventory), its derived tables, set-up, and the mode characters | golden (all 19 modes), hookdiff, port-render-check |
| 0x63685d72 (+ 0x63685d18 retyped) | unit_lattice_apply | the alternative-unit search's rewrite of the phrase: units swapped, phones replaced, inserted, deleted or merged, senones looked up again around each edit | unit (random consistent rule sets, heap compared; the corpus's search never picks a rule), golden, hookdiff |
| 0x63674db7, 0x63674e49, 0x63674df2, 0x63674e4c, 0x63674fcd, 0x6367d9ce, 0x6367d9f4, 0x6367db68, 0x6367dc4d, 0x6367de41, 0x6367de55, 0x6367de5a, 0x6367de79, 0x6367e71b, 0x6367e0ee, 0x6367efa0, 0x6367f0c9 | list_*, voice_read_header, mode_info, mode_info_cfg, cfg_*, mode_find, voices_enum, cfg_load, mode_table_add | the voices: *.vce headers and their CFG characters, *.cfg files, SAPI mode descriptions (TTSMODEINFO), the rank marks, the shared voice table with the characters' data | golden, hookdiff, port-render-check |
| 0x636847ce, 0x6368488f, 0x63684967, 0x636851da, 0x63681401, 0x63671000, 0x63674cd8 | queue_ctor, queue_set_name, queue_set_high, unit_ctor, voc_engine_ctor, ... | constructors of the queues and the stage objects | golden, hookdiff, port-render-check |

Not decompiled in the front end: the alternative prosody of mode 0 (0x636798ce, 0x6367930a,
0x636787f9, 0x6367877e and helpers, ~5 KB; the engine always opens mode 1), the text mode that
0x636744c4 converts (never used by SAPI's text calls), and the user lexicon file reader 0x636751e0
(the emulator has no such file).

## Done, session 6 (23 functions)

| address(es) | name(s) | what | verified by |
|---|---|---|---|
| 0x63683791, 0x63687e15 | fe_get_pitch, unit_get_rate | ITTSAttributes::PitchGet / SpeedGet (PitchSet / SpeedSet were already C) | golden, hookdiff, port-render-check (the 8 speed/pitch renders) |
| 0x636798ce, 0x6367930a, 0x636787f9, 0x6367877e, 0x63679b9f, 0x63679be9, 0x636787d0, 0x63678700, 0x63678aaf, 0x63679c29, 0x63679d05, 0x63678740 | pros_alt_a..d, alt_* | the word module's alternative prosody (mode 0): word records from the tapes, groups between phrase starts, a random step of each word's start/end pitch within the voice's table, cosine interpolation, Hz | unit only (random word tapes; each pass original then C, heap and rand state compared; negative controls caught): the engine never opens mode 0 |
| 0x636751e0, 0x6367e6d7, 0x636750ad, 0x6367486a, 0x6367456e, 0x63674af3, 0x63674bf7, 0x63674be0, 0x63674cc1 | user_lex_load, module_dir, lex_word_bad, old_code, old_codes, lex_pron_convert, lex_pron_names, free_if_* | the per-user lexicon file and the pronunciation checks | userlex-check (11 good and broken files: original, hook build with heap, portable), unit (random phone strings) |

Portable renderer (port/render.c): pitch and speed as a SAPI client sets them (read both, probe their
limits by setting the extremes, restore, set the requested values clamped); tagged text; the front
end's own rand state; the module path for the user lexicon. port/mkvoices.c generates the voice-file
table for the fully baked build (`build/port_render_full`).

## Done, session 7: the library

* **Instances.** Each engine owns its queues, front end, unit stage, synthesizer, pitch and speed, and
  the four MSVCRT rand states of the original's threads (set-up, front end, unit stage, synthesizer;
  each starts at 1, which is what makes an engine's output independent of what came before it).
* **What is shared**, as it was between engines of one process in the original: the voice table
  (voice data loaded once per voice), the lexicons (the user lexicon is read by the first engine), the
  rule machines' tapes, the phone tables; plus this port's own tables (events, directory listings, the
  compiled-in file table). All engine work therefore runs under one process-wide lock: engines on
  different threads take turns, each unaffected by the other (the golden set on four threads is
  identical). s4_stop is lock-free.
* **Streaming.** The front end turns a text into one or a few phrase items; the library hands those to
  the unit stage one at a time, and the unit stage's output to the synthesizer one unit at a time,
  delivering PCM after each: a stop takes effect within one unit (tens of milliseconds of audio).
* **Teardown and limits.** The original's destructors are not decompiled: s4_close frees the queues,
  their items and the events; inner buffers stay (tens of kilobytes per engine), and the engine's
  128 word objects bound a process to 127 engines over its lifetime (s4_open then fails cleanly).
* **Unreached code.** The functions with no C (the older senone-tree format, the text mode of
  0x636744c4, the sample-rate converters) abort if called; the library never asks for another output
  rate, and the shipped voices use the newer tree format.

## The front end, mapped (sessions 3 to 5)

Session 3 inferred roles from call counts on probe texts; session 4 read the code, which corrects one
of them: the "nonword-only driver" at 0x63698f1a.. is two of the dictionaries below (morpheme / affix
lists consulted when the main lexicon has no entry), not a separate letter-to-sound driver.

| part | role | status |
|---|---|---|
| rule machine A (0x6368c997..) | text normalisation rules (numbers, abbreviations, prosody tags) | done, with its procedures and the per-phrase driver around it (session 5) |
| rule machine B (0x636909b7..) | post-lexical and phrasing rules (pauses, word classes) | done, with its 14 procedures |
| rule machine C (0x63693ef8..) | letter-to-sound and dictionary lookup: its procedures call the dictionaries | done, with its 59 procedures |
| main lexicon (0x6369986b, trie 0x63699bd5..0x6369a15f) | a letter trie coded as Huffman fields in a bit stream (0x636fdbb8, 111 KB), entry -> 18-bit offset (0x63722090) into a digram-coded pronunciation table (0x636d4340, 164 KB), with back-references between entries | done |
| ten dictionaries (0x6369a4ea, 0x6369773d, ..., 0x636972c7) | sorted packed-key word lists by length and first letter (5- or 6-bit letter codes), each entry -> an n-bit offset into a digram-coded pronunciation; up to three fields ('!', '#' or '"' separated) written to tapes A, C, D | done: one C function, the variants described by data (row layout, search width, field order) |
| word modules 0x63696cd4.. and 0x6369aaa4 (5.4 KB), 0x6369c9e6 (1.6 KB) | the word loop, the sentence splitter and its lexer | done (session 5) |
| text input, reader, phrase output, thread (0x636840f3, 0x63689357, 0x6367764f, 0x636837a4, 0x63684233 ...) | tags into escapes, the word module's tape, the phone list, the thread | done (session 5) |

Rule bytecode and the lexicon tables are data (A about 3.4 KB at 0x636a2519, B 7.6 KB at 0x636a5ea1,
C 29 KB at 0x636af2a1; lexicon 164 + 111 + 71 KB, dictionaries 20-60 KB each); the portable build will
load them from the DLL, like the voice data.

## Pitfalls found so far

* **Implicit return values.** `codec_bytes_to_floats` looks void, but its caller adds EAX (the index
  it stopped at) to its stream pointer. The unit test passed and the golden corpus failed; the fix
  was to return it. `tools/eaxuse.py` now lists, for any function, which call sites read EAX / EDX /
  ST0 after the call - run it before declaring a function void.
* **Float stack slots.** MSVC spills some x87 intermediates to 4-byte stack slots; each is a rounding
  to float that has to be reproduced (`tools/dataflow.py` prints every store with `f32()` marked).
* **Negative control.** Changing one rounding in dsp_allpole (float instead of double subtraction)
  makes `make check` fail with 99.44% of samples identical and max diff 1 LSB - the harness sees
  single-LSB errors.
* **The compiler rewrites libm calls.** clang turned `pow(10.0, x)` into `exp10(x)`, 1 ulp off in some
  cases (the unit test caught it at -8 dB). All code builds with `-fno-builtin`.
* **Sleep(0) and rand() have state.** `Sleep` advances the emulator's virtual clock and yields; `rand()`
  is per guest thread. The hook build routes `vc_sleep` / `vc_rand` / `vc_malloc` to the emulator's
  own implementations (`harness/hooks.c`), the portable build to trivial ones (`src/crt_vc.c`).
* **Pointers in structs.** `src/gptr.h`: a field is `GPTR(T)` - a 32-bit guest address in the hook
  build (so layouts are the original's, statically asserted with `LAYOUT`) and a real pointer in the
  portable build; code converts with `GP()` / `GPSET()`. DLL globals are `DLLVAR(T, addr)`.
* **Uninitialised stack.** `voc_unit_decode` could read a spectrum left on the stack by an earlier
  call if a delta-coded frame came before any full frame; the C zero-fills instead and the hook build
  warns (`decomp WARNING`) if that ever happens. It never has in any render so far.
* The emulator's x87 is IEEE double with 53-bit precision (the Win32 default control word), and its
  CRT math uses the host libm - so native C with doubles and `-ffp-contract=off` matches it exactly.
  (The emulator itself is bit-exact against the genuine engine on the tetyys references.)
* **Heap identity catches what audio does not.** Comparing the guest heap after a render with and
  without hooks found a macro that evaluated `realloc` twice (GPSET, fixed) - the PCM was identical.
  `tools/hookdiff.sh` now requires identical heaps too.
* **Yields must happen where the original yields.** Heap identity then showed that a hooked
  function that calls Sleep(0) changes the thread interleaving if the yield is deferred to its
  return. Hooks marked `HOOKY` run their C on a per-guest-thread coroutine (ucontext) that is parked
  at the Sleep and resumed when the guest thread is scheduled again (the trap instruction simply
  re-executes). The same mechanism can block (EnterCriticalSection, waits), which makes the thread
  plumbing hookable after all.
* **32-bit address arithmetic.** `DLLVAR` added the load delta to a host pointer instead of to the
  32-bit guest address; it happened to work where the result was converted back to a guest address,
  and read garbage (ASLR-dependent) elsewhere. Fixed; every guest address sum is now done in uint32.
* **x87 branch conditions are NaN-exact** (`x87_je`/`x87_jb`/... in `src/x87.h`): an unordered compare
  reads as "equal" and "below" at the same time, which the C translation must reproduce.
* **Sub-integer differences can be invisible to the corpus.** The prosody contours are rounded to
  int16 before they reach the synthesizer: a deliberate 1e-4 error there left the golden corpus
  identical. Those functions are verified by unit tests on their raw output (which catch it).
* **The original can hang on inputs it never gets.** Contour pass B loops forever on phrases pass A has
  not normalised; its unit test first runs the original pass A (as the engine does).
* **Functions under 3 bytes cannot be hooked** (the trap is 3 bytes); three 1-byte `ret` functions in
  the front end are left out of the counts.
* **Unreachable-in-practice inputs** where C and original must differ are marked: a tag starting with
  "=" makes the original call address 1 (C returns E_FAIL); a > 1023-character tag and a
  delta frame with no preceding full frame read stale stack in the original (C reads zeros). The hook
  build prints `decomp WARNING` if any of these ever happens; none has in any render.

* **Blocking in a hook.** A decompiled thread loop that waits must park where the original waits;
  `vc_WaitForMultipleObjects` / `vc_EnterCriticalSection` poll with the emulator's own wait logic and
  park the coroutine until they succeed. Called outside a `HOOKY` hook they fault, on purpose.
* **Originals that read past their arrays.** `vecarray_append` accepts an index equal to the count
  and reads one element past the end; random tests stay inside what the engine can produce.
* **Random bytecode can hang the original.** Unit tests of the rule machine generate structured
  bytecode (every block closed, filler after each closer) and abort if the original faults, since a
  faulted emulator CPU leaves the next comparison meaningless.

* **A null guest pointer is not NULL in C.** In the hook build `GP(T, 0)` is the start of guest memory,
  not a null host pointer; a loop that stopped at `!GP(...)` never ended. Linked lists keep their links
  as the raw stored values (`EvLink`) and test those.
* **Host locals are not guest memory.** Storing the address of a C local into a guest-pointer field
  (`GPSET`) hands the original an address it cannot read; such locals are passed as host pointers
  and converted in the adapter, or placed on the guest stack (`vc_stack_push`).
* **longjmp.** The word module's reader unwinds errors with setjmp/longjmp through three functions.
  In C a host setjmp/longjmp is exact as long as every frame in between is C, so those three are
  verified through their caller only (marked `HOOKIN` in the hook list, counted by `tools/progress.py`).
* **Functions that look void.** `voice_rec` (0x6367de02) returns a pointer to the record's data, which
  one caller uses; the hook without it failed only `make check`'s RoboSoft reference.
* **List order.** One of the six event-adding functions puts its node at the head of the list, the
  others at the tail; the unit test comparing the event lists found it.
* **Stale locals in the original.** A few paths read a local no one wrote (the escape formatted last,
  a number an escape did not carry); the engine never produces those inputs, and the tests avoid them.
* **Tests and the original's state.** A test that runs the original and then the C from the same
  starting point must also restore what the original changed outside the obvious outputs (here the
  event free list's links); otherwise the second run starts elsewhere.
* **The portable build's own traps.** Tables in the DLL's data that hold pointers must be read field by
  field (`DLLPTR`), not as C structs whose pointers are wider; element sizes written as numbers (a
  queue's 20-byte items) must be `sizeof`; the portable front end found both.
* **Negative controls and make.** make compares timestamps to the second: a control that edits a
  source and rebuilds within the same second as the previous build gets the old binary. The controls
  delete the binary first.

* **A null guest field is not a null host pointer (again).** `GP(T, 0)` is the guest memory's base in
  the hook build; code that tests the converted pointer needs `GPN` (null for a null field). The
  voice table's first `realloc` of a null table was one case.
* **Host names into guest calls.** A stream name built in a C local must be copied onto the guest
  stack before the emulator's IStorage sees it (`vc_stg_open_stream` does it now).
* **The unit tests' heap restore.** Restoring the heap image left the first run's blocks in memory
  above the snapshot's top; the second run's fresh allocations then saw them. The restore now clears
  that memory (found by the lattice test: a realloc'd block's tail differed).
* **The portable build end to end.** Every struct size written as a number where the struct holds
  pointers (VocVoice, VocUnit, Effect, Allpass, VecArray and its vectors, QItem) had to become
  `sizeof`; the unit stage hands Blob / TagOut records to the queue as QItems, whose union is
  pointer-sized in a portable build, so they are copied field by field there; the effect presets in the
  DLL's data hold pointers (field by field again); a mode record's argument was read even for records
  without one (past the end of the data). AddressSanitizer found each in one run.
* **Threads, single-threaded.** The portable driver runs each stage's own thread loop until it would
  wait forever (`vc_idle_hook`, a longjmp back to the driver). Two things the threads gave for free
  had to be kept: the queues' high-water marks must be out of reach (a full queue would park a
  thread), and each thread has its own MSVCRT rand state starting at 1 - the RoboSoft warble draws on
  the synthesizer thread's, the noise table on the set-up thread's (`vc_rand_use`).

* **Tag records are whole phone records.** The front end writes a tag's text over the 100-byte
  PhoneIn record, not just its 16-byte name; `strcat` into `->name` tripped the C library's object-size
  check (a trap, not a crash report) as soon as a tag was longer than 16 characters.
* **A misread crash.** That trap first looked like the unported alternative prosody aborting, and
  12 functions were decompiled on the strength of it; a trace showed the engine never opens prosody
  mode 0. They stay, verified by a unit test, but the lesson is the usual one: trace before acting.
* **Every thread has its own rand.** The alternative prosody and the RoboSoft warble draw on their
  threads' MSVCRT rand states; the portable driver keeps one per stage (front end, unit stage,
  synthesizer), and the set-up thread's for the noise table.
* **Two-character lookups need a terminator.** The older phone-code lookup builds "xy" in a 3-byte
  local whose third byte was zeroed at entry; a 2-byte C array compared garbage (found by the unit
  test, after the render check had shown a lexicon that loaded in one build and not the other).

## What is left, and the obstacles

Reached and not yet decompiled: 91 functions, 10.4 KB - the SAPI/COM shell (which the library
replaces), the C runtime helpers, nothing on the text-to-PCM path. Never reached by the corpus: ~440
functions, ~38 KB (the sample-rate converters, the older senone-tree format, the lexicon COM methods,
error paths).

For an app: an Objective-C/Swift wrapper over lib/sapi4tts.h (AVAudioEngine playback, voice list),
an xcframework (macOS + iOS device + simulator: `make ios-check` builds the device slice only), and a
decision on shipping the voices baked (one 9 MB object) or as files in the bundle. Memory: engines
are meant to be long-lived (see the limits above); an app should keep one per voice.

**Pointers.** The emulator is 32-bit guest on a 64-bit host, so C structs holding pointers cannot be
shared between guest and native code the way OpenTV's 32-bit hook build shares them. Leaf functions
take guest pointers translated at the adapter. Objects with pointer fields use `GPTR(T)` (src/gptr.h):
a 32-bit guest address in the hook build and a real pointer in the portable build, so the same source
compiles both ways (`clang -fsyntax-only src/*.c` without DECOMP_HOOK passes). Globals that hold
pointers are accessed with `DLLPTR(T, addr)`: a 32-bit slot in the hook build, a host-pointer slot in
the portable build (port/dllimage.c, filled from the DLL's relocations).

**Threads.** The engine runs three worker threads with events and critical sections. The hook build
keeps the original threads; the portable renderer runs the same thread loops one after another in
one thread (see the pitfalls), verified against the golden hashes.

## Estimate

The decompilation's goal - the engine's text-to-PCM path in portable C, bit-exact against the original
- is met, with an embedding library. Optional remainder: the unreached converters and tree format (a
day each if ever needed), an xcframework and a Swift wrapper (a session), the original's destructors
(to lift the engine limit; a session).
