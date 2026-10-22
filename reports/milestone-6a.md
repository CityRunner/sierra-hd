# Milestone 6a: vocabulary and Said matching

Status: **awaiting approval**.

Exit criterion (spec section 12): a recorded set of typed commands gives the
same results as ScummVM.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 6a: vocabulary and
Said matching", and the file format in `docs/formats/replay-and-trace.md`,
"Parser files".

1. **The parser is compared directly.** Until now a parser difference showed
   only through its effects on the kernel calls. Both engines now also write
   one line for each kParse call and one for each kSaid call:
   - **kParse:**
     - the input as typed;
     - the outcome: parsed, rejected by the grammar, unknown word, or no
       words;
     - the words the grammar got (class and group), after the room's
       synonyms and the pronouns;
     - the parse tree the Said specs are matched against, or the unknown
       word;
     - the event's type and `claimed` flag.
   - **kSaid:** the spec's bytes; how it matched (full, partial, none, or
     skipped because the event was already claimed); the `claimed` flag it
     leaves.

   The reference writes these lines from `kparse.cpp` (patch 0003,
   regenerated), and sci-vm through a new hook, `RunOptions::on_parser`.
   `tools/vm-trace/run-scenario.sh` compares them as a fifth item, after the
   traces, screens, maps and actor positions
   (`tools/vm-trace/compare-parser.py`).
2. **A corpus of typed commands, generated from the vocabulary.**
   - `sci-vm-trace --vocabulary` lists vocab.000, the suffixes of vocab.901
     and every script's synonyms, as sci-text reads them.
   - `tools/vm-trace/make-parser-scenarios.py` turns that list into seven
     replays:
     - **two for the sentence forms** (`parser-forms`, `parser-forms-2`):
       - verbs alone and with objects, articles, prepositions, adjectives,
         adverbs and conjunctions;
       - pronouns: before any noun, after a noun, after a failed parse;
       - the room's synonyms;
       - several sentences in one line, punctuation and capitals;
       - accented bytes, long and empty input, unknown words, numbers and
         broken grammar.
     - **five that type every word** (`parser-words-*`), in five rooms with
       their own Said specs and synonyms:
       - Laura's room (44);
       - the back upstairs hallway (43);
       - the front downstairs hallway (37);
       - the kitchen (35);
       - the back garden (4).
3. **One fix**, in the replay reader rather than the parser. Before the text
   of a `text` event, sci-vm skipped only one space; it now skips all white
   space, as the reference does.

## Results

### Scenarios: five comparisons

All 31 runs of `tests/scenarios/scenarios.txt` were run in the reference
and in sci-vm and compared on all five items. Everything matched. The runs
were repeated in the main checkout after the merge, with the reference
rebuilt from the updated patch.

| Scenario | Stop tick | Seed | Kernel calls | Screens and maps | Actor frames | kParse | kSaid (full / partial) |
|---|---|---|---|---|---|---|---|
| boot | 3,600 | 0 | 12,022 | 61 | 606 | 0 | 0 |
| boot | 3,600 | 7 | 12,022 | 61 | 606 | 0 | 0 |
| boot | 3,600 | 12345 | 12,022 | 61 | 606 | 0 | 0 |
| boot | 18,000 | 0 | 57,622 | 301 | 3,006 | 0 | 0 |
| mouse | 3,600 | 0 | 13,864 | 31 | 157 | 0 | 0 |
| text | 3,600 | 0 | 34,624 | 61 | 111 | 0 | 0 |
| copy-protection | 6,000 | 0 | 57,406 | 101 | 161 | 0 | 0 |
| menu-keys | 3,600 | 0 | 29,745 | 61 | 224 | 0 | 0 |
| intro | 30,000 | 0 | 227,303 | 501 | 4,759 | 0 | 0 |
| mansion-walk | 11,000 | 0 | 81,728 | 184 | 1,723 | 0 | 0 |
| menus | 9,030 | 0 | 67,952 | 151 | 1,107 | 0 | 0 |
| bedroom-wait | 20,000 | 0 | 119,178 | 334 | 3,318 | 0 | 0 |
| parser-look | 7,700 | 0 | 74,717 | 129 | 920 | 8 | 1,128 (8 / 16) |
| parser-verbs | 7,700 | 0 | 73,547 | 129 | 930 | 8 | 899 (6 / 2) |
| parser-errors | 7,700 | 0 | 73,875 | 129 | 920 | 8 | 941 (3 / 11) |
| mouse-walk | 12,600 | 0 | 109,769 | 211 | 1,400 | 2 | 232 (2 / 7) |
| grounds | 12,600 | 0 | 110,283 | 211 | 1,532 | 2 | 283 (2 / 2) |
| inventory-and-actions | 24,000 | 0 | 226,195 | 401 | 2,394 | 11 | 1,286 (12 / 33) |
| boot-and-bedroom | 5,000 | 7 | 39,019 | 84 | 730 | 2 | 283 (2 / 3) |
| walls-keyboard | 9,300 | 0 | 61,779 | 156 | 1,522 | 0 | 0 |
| mouse-obstacles | 8,500 | 0 | 56,963 | 142 | 1,383 | 0 | 111 (0 / 0) |
| stairs | 8,200 | 0 | 59,428 | 137 | 1,326 | 0 | 60 (0 / 0) |
| lillian-walk | 17,400 | 0 | 107,135 | 291 | 2,865 | 0 | 31 (0 / 0) |
| attic | 9,400 | 0 | 76,231 | 157 | 1,119 | 0 | 80 (0 / 0) |
| **parser-forms** (new, room 44) | 15,122 | 0 | 249,195 | 253 | 1,394 | 96 | 10,008 (73 / 112) |
| **parser-forms-2** (new, room 37) | 19,555 | 0 | 305,534 | 326 | 1,913 | 116 | 15,763 (24 / 214) |
| **parser-words-hallway** (new, room 43) | 39,821 | 0 | 785,671 | 664 | 3,514 | 401 | 54,747 (103 / 339) |
| **parser-words-garden** (new, room 4) | 39,854 | 0 | 715,093 | 665 | 3,329 | 268 | 41,870 (78 / 707) |
| **parser-words-kitchen** (new, room 35) | 39,803 | 0 | 647,968 | 664 | 3,566 | 206 | 41,564 (39 / 890) |
| **parser-words-bedroom** (new, room 44) | 24,024 | 0 | 495,499 | 401 | 2,058 | 199 | 38,938 (43 / 1,057) |
| **parser-words-downstairs** (new, room 37) | 22,057 | 0 | 378,735 | 368 | 2,046 | 135 | 25,054 (37 / 592) |

31 runs: every trace, screen, map, actor frame and parser line identical.

- **Coverage of the seven new runs:**
  - 1,421 kParse calls: 1,362 parsed, 24 rejected by the grammar, 32 with an
    unknown word, 3 with no words.
  - 227,944 kSaid calls: 397 full matches, 3,911 partial, 218,407 no match,
    5,229 skipped.
- **The comparison catches differences.** In a kept parser-look run,
  turning one full Said match into a partial one, or changing one node of a
  parse tree, makes `compare-parser.py` report that call and exit with 1.

### Vocabulary coverage

All 1,295 words of vocab.000 are typed at least once.

- **1,289 are recognized.** The other six cannot be, in ScummVM either:
  - the Said wildcards `!*` and `*`, which the tokenizer drops as
    punctuation;
  - four words stored with a dash or an apostrophe: `good-looking`,
    `manor-house`, `o'neil` and `run-down`. The lookup removes dashes and the
    tokenizer drops apostrophes, so the stored spelling never matches.
- **1,287 appear in at least one command the grammar accepts.** The two
  that do not are the number word `10` and `noword`.

| Class | Words typed (recognized, where fewer) |
|---|---|
| Nouns | 770 (768) |
| Imperative verbs | 218 |
| Adjectives | 122 (120) |
| Adverbs | 51 |
| Prepositions | 37 |
| Noun-verbs | 34 |
| Indicative verbs | 21 |
| Pronouns | 11 |
| Conjunctions and linking words | 11 |
| Articles | 6 |
| Noun-indicative verbs | 5 |
| Other words with several classes (back, front, open, gold) | 4 |
| Wildcards (`any`, `anyword`, `*`) | 3 |
| Other (`!*`, `noword`) | 2 |

**No parser difference was found.** sci-text and sci-vm already gave the
reference's results for every command, so the parser code needed no change.

## Tests and style

- **Local checks.** `scripts/pre-push.sh` passes on the merged branch:
  - clang-format, clang-tidy, the repository policy and the hint check;
  - all test suites in the release, ASan+UBSan and TSan builds;
  - fuzzing;
  - all tests on ARM64 under qemu-user.
- **New unit tests:**
  - `tests/unit/text/parser_test.cpp` pins what the corpus showed both
    engines do:
    - words stored with a dash or an apostrophe never match;
    - bytes above 127 count as letters, and code page 437's accented
      capitals are lowered;
    - a tab is dropped.
  - `tests/unit/vm/input_test.cpp` covers the white space before a `text`
    event.

## CI minutes

CI_MINUTES

## Deviations from the spec

None.

## Open questions

None. Two notes for later milestones:
- `tools/vm-trace/run-all-scenarios.sh` runs the scenarios one at a time.
  I ran them four at a time through `run-scenario.sh` instead; the 31 runs
  took about 8 minutes on four cores. A `--jobs` option is an obvious
  addition when the scenario set grows again.
- Some corpus commands are kept as test data although the grammar rejects
  them, for example "look at the biggest bed". Both engines reject them in
  the same way.
