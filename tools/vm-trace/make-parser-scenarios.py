#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generates the parser scenarios of milestone 6a from the game's vocabulary.

    make-parser-scenarios.py <vocabulary file> <scenario folder> [--words <file>]

The vocabulary file is what `sci-vm-trace --vocabulary <game> <file>` writes:
every word of vocab.000 with its class and group, the suffixes of vocab.901
and the synonyms each script sets (sci-text reads them). The script writes
tests/scenarios/parser-*.txt (replays, docs/formats/replay-and-trace.md):
each one walks Laura into a room along the route of an earlier scenario,
then types commands, each closed with Enter and a second Enter (Escape
for quit, restart, save and restore) before the next. Together they type
every word of the vocabulary at least once, in a sentence that fits its
class, besides the synonyms of each room, pronouns, several sentences in
one line, punctuation, capitals, long input, empty input, unknown words and
broken grammar. It prints the lines for tests/scenarios/scenarios.txt
(file, stop tick, seed). With --words, it lists which scenario and command
first types each word.

The output depends only on the vocabulary file, so running the script again
gives the same files.
"""

import argparse
import collections
import pathlib
import sys

# Word classes as vocab.000 stores them (sci/text/parser.hpp, shifted by 4).
PREPOSITION = 0x010
ARTICLE = 0x020
ADJECTIVE = 0x040
PRONOUN = 0x080
NOUN = 0x100
INDICATIVE_VERB = 0x200
ADVERB = 0x400
IMPERATIVE_VERB = 0x800

# Ticks from a command to the key that closes its message (plus a tick per
# character typed), to a second Enter for a second message (an Enter with
# no message on screen does nothing), and to the next command.
CLOSE = 40
SECOND = 20
NEXT = 20
# Commands after which Laura walks before the message shows, and the ticks
# to wait for it. Found by running the scenarios: the parser file shows
# whether each command reached kParse as typed.
SLOW = {"open it": 300, "open the bag": 300, "open the case": 300}
# Commands with these words are closed with Escape instead of Enter: they
# may ask a question whose default button would quit, restart, save or
# restore (Escape cancels it, and closes a message as Enter does).
ESCAPE = {"quit", "restart", "restore", "save"}
# Stop ticks stay at or below 40000. In Laura's room, Lillian comes in at
# about tick 24400 and a conversation follows whose messages close on a
# key, which would take the commands typed then; the commands stop before.
LAST_TICK = 39800
LAST_TICK_BEDROOM = 24000

COPY_PROTECTION = """\
# Copy protection (seed 0): leaves the title, moves the pointer to the
# right answer and confirms; Enter on the message that follows.
600 key 13
650 key 20480
670 key 20480
690 key 20480
710 key 20480
730 key 20480
750 key 19712
770 key 19712
790 key 18432
810 key 18432
830 key 13
831 key 13
"""

# The way into each room, from the earlier scenarios, and the first tick
# at which a command can be typed there.
ROUTES = {
    "bedroom": (
        44,
        """\
# "Have you previously attended?": Yes. Laura's room (44) is playable from
# about tick 4000 (parser-look.txt).
1520 key 13
""",
        4500,
    ),
    "hallway": (
        43,
        """\
# "Have you previously attended?": Yes. Laura's room (44): two clicks to
# the west door, into the back upstairs hallway (43); a click closes its
# description (attic.txt).
1000 key 13
3400 move 105 143
3402 down 105 143 0
3406 up 105 143 0
3700 move 0 157
3702 down 0 157 0
3706 up 0 157 0
4000 down 160 100 0
4004 up 160 100 0
""",
        4300,
    ),
    "downstairs": (
        37,
        """\
# "Have you previously attended?": Yes. The route of mouse-walk.txt: 44
# (looks at the armchair) -> 45 (the dollhouse) -> 44 -> 43 -> 47 -> down
# the right staircase into the front downstairs hallway (37); a click closes
# its description, and Laura walks down the stairs.
1000 key 13
3500 move 110 135
3502 down 110 135 3
3506 up 110 135 3
3800 down 110 135 0
3804 up 110 135 0
3900 move 211 133
3902 down 211 133 0
3906 up 211 133 0
4000 move 300 133
4002 down 300 133 0
4006 up 300 133 0
4400 down 160 100 0
4404 up 160 100 0
4500 move 252 110
4502 down 252 110 3
4506 up 252 110 3
4700 down 252 110 0
4704 up 252 110 0
4800 move 0 133
4802 down 0 133 0
4806 up 0 133 0
5000 move 105 143
5002 down 105 143 0
5006 up 105 143 0
5400 move 0 157
5402 down 0 157 0
5406 up 0 157 0
5800 down 160 100 0
5804 up 160 100 0
5900 move 160 199
5902 down 160 199 0
5906 up 160 199 0
6740 down 160 100 0
6744 up 160 100 0
6800 move 215 150
6802 down 215 150 0
6806 up 215 150 0
7100 move 272 148
7102 down 272 148 0
7106 up 272 148 0
7500 move 290 199
7502 down 290 199 0
7506 up 290 199 0
8300 down 160 100 0
8304 up 160 100 0
""",
        8800,
    ),
    "garden": (
        4,
        """\
# "Have you previously attended?": Yes. The route of mouse-walk.txt: 44
# (looks at the armchair) -> 45 (the dollhouse) -> 44 -> 43 -> 47 -> 37 (the
# rug) -> 33 (the grandfather clock) -> out of the French doors into the
# back garden (4), outdoors; clicks close its description and the "creepy"
# warning (inventory-and-actions.txt, 1517 ticks earlier).
1000 key 13
3500 move 110 135
3502 down 110 135 3
3506 up 110 135 3
3800 down 110 135 0
3804 up 110 135 0
3900 move 211 133
3902 down 211 133 0
3906 up 211 133 0
4000 move 300 133
4002 down 300 133 0
4006 up 300 133 0
4400 down 160 100 0
4404 up 160 100 0
4500 move 252 110
4502 down 252 110 3
4506 up 252 110 3
4700 down 252 110 0
4704 up 252 110 0
4800 move 0 133
4802 down 0 133 0
4806 up 0 133 0
5000 move 105 143
5002 down 105 143 0
5006 up 105 143 0
5400 move 0 157
5402 down 0 157 0
5406 up 0 157 0
5800 down 160 100 0
5804 up 160 100 0
5900 move 160 199
5902 down 160 199 0
5906 up 160 199 0
6740 down 160 100 0
6744 up 160 100 0
6800 move 215 150
6802 down 215 150 0
6806 up 215 150 0
7100 move 272 148
7102 down 272 148 0
7106 up 272 148 0
7500 move 290 199
7502 down 290 199 0
7506 up 290 199 0
8300 down 160 100 0
8304 up 160 100 0
8600 move 160 110
8602 down 160 110 3
8606 up 160 110 3
8800 move 290 156
8802 down 290 156 0
8806 up 290 156 0
9000 move 289 166
9002 down 289 166 0
9006 up 289 166 0
9200 move 120 170
9202 down 120 170 0
9206 up 120 170 0
9600 move 160 60
9602 down 160 60 0
9606 up 160 60 0
10800 down 160 100 0
10804 up 160 100 0
10900 move 52 130
10902 down 52 130 3
10906 up 52 130 3
11100 down 52 130 0
11104 up 52 130 0
11500 move 160 95
11502 down 160 95 0
11506 up 160 95 0
12500 down 160 100 0
12504 up 160 100 0
12900 down 160 100 0
12904 up 160 100 0
""",
        13200,
    ),
    "kitchen": (
        35,
        """\
# "Have you previously attended?": Yes. The route and commands of
# inventory-and-actions.txt up to the kitchen (35): 44 -> 43 -> 47 -> 37 ->
# 33 -> the back garden 4 -> the northeast corner 12 -> the kitchen, where
# Laura walks to the left of the stove.
1000 key 13
3500 text look at the dresser
3700 key 13
3800 text open the drawer
4000 key 13
4200 text open my suitcase
4800 key 13
5000 text close my suitcase
5300 key 13
5500 key 9
5700 key 13
6500 move 50 157
6502 down 50 157 0
6506 up 50 157 0
6900 move 0 157
6902 down 0 157 0
6906 up 0 157 0
7317 down 160 100 0
7321 up 160 100 0
7417 move 160 199
7419 down 160 199 0
7423 up 160 199 0
8257 down 160 100 0
8261 up 160 100 0
8317 move 215 150
8319 down 215 150 0
8323 up 215 150 0
8617 move 272 148
8619 down 272 148 0
8623 up 272 148 0
9017 move 290 199
9019 down 290 199 0
9023 up 290 199 0
9817 down 160 100 0
9821 up 160 100 0
10117 move 160 110
10119 down 160 110 3
10123 up 160 110 3
10317 move 290 156
10319 down 290 156 0
10323 up 290 156 0
10517 move 289 166
10519 down 289 166 0
10523 up 289 166 0
10717 move 120 170
10719 down 120 170 0
10723 up 120 170 0
11117 move 160 60
11119 down 160 60 0
11123 up 160 60 0
12317 down 160 100 0
12321 up 160 100 0
12417 move 52 130
12419 down 52 130 3
12423 up 52 130 3
12617 down 52 130 0
12621 up 52 130 0
13017 move 160 95
13019 down 160 95 0
13023 up 160 95 0
14017 down 160 100 0
14021 up 160 100 0
14417 down 160 100 0
14421 up 160 100 0
14517 move 300 176
14519 down 300 176 0
14523 up 300 176 0
14917 move 319 176
14919 down 319 176 0
14923 up 319 176 0
15517 down 160 100 0
15521 up 160 100 0
15617 move 290 72
15619 down 290 72 0
15623 up 290 72 0
15917 move 312 85
15919 down 312 85 0
15923 up 312 85 0
16117 move 312 140
16119 down 312 140 0
16123 up 312 140 0
16417 move 195 182
16419 down 195 182 0
16423 up 195 182 0
16817 move 160 125
16819 down 160 125 0
16823 up 160 125 0
17117 text look in the doghouse
17517 key 13
17717 text inventory
17917 key 13
18117 text open the door
18417 key 13
18617 move 192 138
18619 down 192 138 0
18623 up 192 138 0
18817 move 160 130
18819 down 160 130 0
18823 up 160 130 0
19817 down 160 100 0
19821 up 160 100 0
20017 down 160 100 0
20021 up 160 100 0
20217 down 160 100 0
20221 up 160 100 0
20500 move 40 105
20502 down 40 105 0
20506 up 40 105 0
""",
        21000,
    ),
}


class Vocabulary:
    def __init__(self, path: pathlib.Path) -> None:
        self.words: dict[str, tuple[int, int]] = {}
        self.suffixes: list[tuple[str, str, int, int]] = []
        self.synonyms: dict[int, list[tuple[int, int]]] = collections.defaultdict(list)
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if fields[0] == "word":
                self.words[fields[1]] = (int(fields[2], 16), int(fields[3], 16))
            elif fields[0] == "suffix":
                # An empty suffix is written as nothing between the spaces.
                parts = line.split(" ")
                self.suffixes.append((parts[1], parts[2], int(parts[3], 16), int(parts[4], 16)))
            elif fields[0] == "synonym":
                self.synonyms[int(fields[1])].append((int(fields[2], 16), int(fields[3], 16)))
        self.groups: dict[int, list[str]] = collections.defaultdict(list)
        for word, (_, group) in sorted(self.words.items()):
            self.groups[group].append(word)

    def of_class(self, mask: int, exact: bool = False) -> list[str]:
        return sorted(w for w, (c, _) in self.words.items() if (c == mask if exact else c & mask) and typeable(w))


def typeable(word: str) -> bool:
    """Whether the word is made of letters and digits (others are typed in the forms scenario)."""
    return word.isalnum() and word.isascii()


def coverage_commands(vocab: Vocabulary) -> list[str]:
    """Commands that type every typeable word in a sentence for its class."""
    commands: list[str] = []
    verbs = vocab.of_class(IMPERATIVE_VERB)
    nouns = [w for w in vocab.of_class(NOUN) if not vocab.words[w][0] & IMPERATIVE_VERB]
    adjectives = [w for w in vocab.of_class(ADJECTIVE) if not vocab.words[w][0] & NOUN]
    prepositions = vocab.of_class(PREPOSITION, exact=True)
    adverbs = vocab.of_class(ADVERB, exact=True)
    articles = vocab.of_class(ARTICLE, exact=True)
    indicative = vocab.of_class(INDICATIVE_VERB)
    conjunctions = vocab.of_class(0x004, exact=True)
    linking = vocab.of_class(0x008, exact=True)
    pronouns = vocab.of_class(PRONOUN, exact=True)
    # Verbs alone (also those that are nouns too: "light", "open").
    commands += verbs
    # Verbs with an object, cycling through the nouns: verb + article + noun,
    # verb + noun, verb + preposition + noun, look + preposition + article +
    # noun, verb + article + adjective + noun.
    noun_cycle = iter(nouns)
    remaining = list(nouns)

    def next_noun() -> str:
        noun = next(noun_cycle, None)
        if noun is None:
            return "bed"
        remaining.remove(noun)
        return noun

    for i, verb in enumerate(verbs):
        form = i % 3
        if form == 0:
            commands.append(f"{verb} the {next_noun()}")
        elif form == 1:
            commands.append(f"{verb} {next_noun()}")
        else:
            commands.append(f"{verb} at the {next_noun()}")
    for preposition in prepositions:
        commands.append(f"look {preposition} the {next_noun()}")
    for article in articles:
        commands.append(f"look at {article} {next_noun()}")
    for adjective in adjectives:
        commands.append(f"look at the {adjective} {next_noun()}")
    for adverb in adverbs:
        commands.append(f"look {adverb}")
    for verb in indicative:
        commands.append(verb)
        commands.append(f"{verb} the {next_noun()}")
    for conjunction in conjunctions:
        commands.append(f"look at the {next_noun()} {conjunction} the {next_noun()}")
    for word in linking:
        commands.append(f"look at the {next_noun()} {word} the {next_noun()}")
    for pronoun in pronouns:
        commands.append(f"look at the {next_noun()}")
        commands.append(f"look at {pronoun}")
    while remaining:
        commands.append(f"look at the {next_noun()}")
    # The wildcard words ("any", "anyword") and "noword".
    for word in vocab.of_class(0xFF4, exact=True) + vocab.of_class(0x000, exact=True):
        commands.append(f"look at {word}")
        commands.append(word)
    # One typed form per suffix, from the first word it applies to.
    for alt, stored, mask, result in vocab.suffixes:
        for word in sorted(vocab.words):
            cls = vocab.words[word][0]
            if not typeable(word) or not word.endswith(stored) or not cls & mask or word.isdigit():
                continue
            typed = word[: len(word) - len(stored)] + alt
            if typed in vocab.words or not typed:
                continue
            # The sentence for the class the suffix gives: an adverb after
            # the verb, an adjective before a noun, a noun as the object.
            if result & NOUN:
                commands.append(f"look at the {typed}")
            elif result & ADJECTIVE:
                commands.append(f"look at the {typed} bed")
            else:
                commands.append(f"look {typed}")
            break
    return commands


def synonym_commands(vocab: Vocabulary, room: int) -> list[str]:
    """Every word of each group the room replaces with a synonym."""
    commands = []
    for group, _ in vocab.synonyms.get(room, []):
        for word in vocab.groups[group]:
            if typeable(word):
                commands.append(f"look at the {word}")
    return commands


def forms_commands(vocab: Vocabulary) -> tuple[list[str], list[str]]:
    """Pronouns, synonyms, sentences, punctuation, case, length and empty input; then
    unknown words, numbers and broken grammar."""
    odd_words = sorted(w for w in vocab.words if not typeable(w))
    long_line = "look at the bed and the chair and the fireplace and the painting and the suitcase and the dresser"
    first = [
        # A pronoun before any noun was parsed (no reference yet).
        "look at it",
        # Pronouns and what they stand for: the object of the last parse.
        "look at the bed",
        "look at it",
        "open it",
        "look at lillian",
        "look at her",
        "talk to her",
        "look at the colonel",
        "look at him",
        "talk to him",
        "look at them",
        "look under it",
        "give it to her",
        "look at the suitcase",
        "open it",
        "look at that",
        "look at this",
        "look at these",
        "look at those",
        "look at herself",
        "look at himself",
        "look at themselves",
        # A failed parse keeps the last reference.
        "look the at bed",
        "look at it",
        # The synonyms of Laura's room: chest, dresser, case, bag, gate and
        # laundry are drawers, the suitcase and the chute here.
        *synonym_commands(vocab, 44),
        "open the case",
        "open the bag",
        "look in the dresser",
        "look in the laundry",
        "close the case",
        # Several sentences in one line.
        "look at the bed. look at the chair.",
        "look at the bed, then look at the chair",
        "look and listen",
        "open the suitcase and look in it",
        "look; sit down",
        # Punctuation.
        "look!",
        "look?",
        "look!!!",
        "look at the bed!",
        "Look... at the bed?",
        '"look"',
        "(look)",
        "look, bed",
        "look at the bed's drawer",
        "look's",
        "look:at:the:bed",
        "look-at the bed",
        "look at the -bed",
        "look-",
        "lo-ok at the be-d",
        "look at the #bed",
        "look @ the bed",
        "look at the bed & chair",
        "look at the bed + chair",
        "look at the bed/chair",
        "look at the 'bed'",
        "look at the [bed]",
        "look at the {bed}",
        "look at the <bed>",
        "look at the bed=chair",
        "look at the bed%",
        "look at the bed~",
        "look at the bed|chair",
        "look at the bed\\chair",
        "look at the bed*",
        "look at the bed^2",
        "look at the bed`",
        "look at the bed$",
        # Capitals.
        "LOOK",
        "LOOK AT THE BED",
        "Look At The Bed",
        "lOoK aT tHe BeD",
        "Look at Lillian",
        # Bytes above 127 (each UTF-8 byte is a key): word characters, with
        # the accented capitals of code page 437 made lower case.
        "look at the caf\u00e9",
        "LOOK AT THE \u00c0\u00ce\u00cf\u00d0\u00d2\u00d9\u00da",
        "look at the \u00a5 bed",
        # Long input: the input line takes what fits.
        long_line,
        long_line.upper(),
        "look" + " look" * 20,
        "a" * 60,
        "supercalifragilisticexpialidocious",
        # Empty input: no words at all, and an Enter alone (no input line).
        ".",
        "?!",
        ", , ,",
        "",
    ]
    second = [
        # Unknown words.
        "xyzzy",
        "plugh",
        "look at the xyzzy",
        "xyzzy the bed",
        "look at the bedd",
        "lok at the bed",
        "look at teh bed",
        "look at the bed xyzzy",
        "frobnicate",
        # Words of the vocabulary that cannot be typed as stored (a dash or
        # an apostrophe), and what typing them gives.
        *(f"look at the {w}" for w in odd_words),
        "look at the oneil",
        "look at the goodlooking bed",
        "look at the manorhouse",
        # Numbers: those of the vocabulary and others.
        "look at 42",
        "look at 0",
        "look at 007",
        "look at 1000000",
        "look at 99999999999",
        "look at 3 beds",
        "1",
        "look at 1st",
        # Broken grammar.
        "the",
        "the bed",
        "bed",
        "bed look",
        "the bed look",
        "at the",
        "look at",
        "look at the",
        "look at at the bed",
        "look the the bed",
        "look at the the bed",
        "bed bed bed",
        "look look",
        "at",
        "and",
        "look at the bed and",
        "and look",
        "quickly",
        "red",
        "look at red",
        "look at the red",
        "it",
        "look it",
        "with the key",
        "open the door with",
        "give to lillian",
        "give the bed to",
        "a an the",
        "yes",
        "no",
        "please look",
        "look please",
        "look at the bed please",
        "don't look",
        "not look",
        "look not",
        "do look at the bed",
        "is the bed open",
        "where is lillian",
        "who is lillian",
        "what is this",
        "why",
        "how",
        "can i look",
        "will you look",
        "i look",
        "you look",
        "lillian, look at the bed",
        "tell lillian to look at the bed",
        "ask lillian about the colonel",
        "ask about the colonel",
        "talk",
        "talk to",
        "say hello",
        "say hello to lillian",
        # Words that also are other classes, in each place.
        "open",
        "open the open",
        "light the light",
        "look at the light",
        "lock the lock",
        "drink the drink",
        "walk",
        "walk to the bed",
        "back",
        "go back",
        "look at the back",
        "front",
        "look at the front",
        "gold",
        "look at the gold",
        "look at the gold bed",
        "will",
        "can",
        "does",
        "look at the bequest",
        # And a plain command at the end, to see the parser is still well.
        "look at the bed",
    ]
    return first, second


def write_scenario(
    path: pathlib.Path, title: str, room_name: str, commands: list[str], words: dict[str, str]
) -> tuple[int, list[str]]:
    """Writes one scenario; returns its stop tick and the commands left over."""
    room, prefix, tick = ROUTES[room_name]
    last_tick = LAST_TICK_BEDROOM if room_name == "bedroom" else LAST_TICK
    lines = [
        "sierra-hd replay 1",
        f"# {title}",
        "# Generated by tools/vm-trace/make-parser-scenarios.py from the game's",
        "# vocabulary; do not edit by hand.",
        COPY_PROTECTION.rstrip("\n"),
        prefix.rstrip("\n"),
        f"# Room {room}: each command is typed, then its message closed with Enter",
        "# and a second Enter for a second message (Escape for the questions of",
        "# quit, restart, save and restore).",
    ]
    left = list(commands)
    while left:
        command = left[0]
        close = SLOW.get(command, CLOSE + len(command))
        slot = close + SECOND + NEXT
        if tick + slot > last_tick:
            break
        left.pop(0)
        lines.append(f"{tick} text {command}".rstrip())
        if ESCAPE & set(command.lower().split()):
            # Escape cancels the question; a second one would open the menus.
            lines.append(f"{tick + close} key 27")
        else:
            lines.append(f"{tick + close} key 13")
            lines.append(f"{tick + close + SECOND} key 13")
        for word in command.lower().split():
            words.setdefault(word, f"{path.name}: {command}")
        tick += slot
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return tick + 100, left


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("vocabulary", type=pathlib.Path)
    parser.add_argument("folder", type=pathlib.Path)
    parser.add_argument("--words", type=pathlib.Path)
    args = parser.parse_args()
    vocab = Vocabulary(args.vocabulary)
    words: dict[str, str] = {}
    plan = []
    # Each list of commands fills its scenarios in turn; each scenario starts
    # with the synonyms of its room (in Laura's room, the forms test them).
    forms, more_forms = forms_commands(vocab)
    batches = [
        (
            forms,
            [("parser-forms.txt", "bedroom", "Pronouns, synonyms, sentences, punctuation, case, length, empty input.")],
        ),
        (
            more_forms,
            [("parser-forms-2.txt", "downstairs", "Unknown words, numbers, broken grammar, words of several classes.")],
        ),
        (
            coverage_commands(vocab),
            [
                ("parser-words-hallway.txt", "hallway", "The vocabulary, part 1: in the back upstairs hallway (43)."),
                ("parser-words-garden.txt", "garden", "The vocabulary, part 2: outdoors, in the back garden (4)."),
                ("parser-words-kitchen.txt", "kitchen", "The vocabulary, part 3: in the kitchen (35)."),
                ("parser-words-bedroom.txt", "bedroom", "The vocabulary, part 4: in Laura's room (44)."),
                (
                    "parser-words-downstairs.txt",
                    "downstairs",
                    "The vocabulary, part 5: in the front downstairs hallway (37).",
                ),
            ],
        ),
    ]
    for commands, scenarios in batches:
        for name, route, title in scenarios:
            room = ROUTES[route][0]
            extra = [] if name == "parser-forms.txt" else synonym_commands(vocab, room)
            stop, commands = write_scenario(args.folder / name, title, route, extra + commands, words)
            plan.append((name, stop))
            if not commands:
                break
        if commands:
            print(f"{len(commands)} commands do not fit", file=sys.stderr)
            return 1
    for name, stop in plan:
        print(f"{name} {stop} 0")
    if args.words:
        lines = [f"{word} {words.get(word, '-')}" for word in sorted(vocab.words)]
        args.words.write_text("\n".join(lines) + "\n")
    typed = sum(1 for word in vocab.words if word in words)
    print(f"{typed} of {len(vocab.words)} words typed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
