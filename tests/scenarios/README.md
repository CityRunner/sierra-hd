# Scenario recordings

Replays (docs/formats/replay-and-trace.md) that are run in the reference
ScummVM and in sci-vm and must give the same kernel-call trace, screens,
1x priority and control maps and actor positions:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        tools/vm-trace/run-scenario.sh tests/scenarios/<name>.txt <stop tick> build/scenarios/<name>

All use seed 0.

| Scenario | Stop tick | What it covers |
| --- | --- | --- |
| `parser-look.txt` | 7700 | Passes the copy protection, then types "look" commands in Laura's room: plain, with articles, a plural suffix and a pronoun (kParse, kSaid). |
| `parser-verbs.txt` | 7700 | Same start; verbs other than look, ending with one that makes Laura walk. |
| `parser-errors.txt` | 7700 | Same start; an unknown word (wordFail), broken sentences (syntaxFail), punctuation, upper case, a number and a dashed word. |
| `walls-keyboard.txt` | 9300 | Keyboard walks into walls and furniture, the diagonals, walk-stop-walk, rooms 44, 45 and 43. |
| `mouse-obstacles.txt` | 8500 | Mouse walks into furniture and round it, turns mid-walk, rooms 44, 45 and 43. |
| `stairs.txt` | 8200 | Both grand staircases of rooms 47 and 37, down and up, turned mid-flight. |
| `lillian-walk.txt` | 17400 | Lillian comes in while Laura is in her way; Laura walks into her and round her. |
