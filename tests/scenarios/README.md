# Scenario recordings

Replays (docs/formats/replay-and-trace.md) that are run in the reference
ScummVM and in sci-vm and must give the same kernel-call trace and screens:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        tools/vm-trace/run-scenario.sh tests/scenarios/<name>.txt <stop tick> build/scenarios/<name>

All use seed 0.

| Scenario | Stop tick | What it covers |
| --- | --- | --- |
| `parser-look.txt` | 7700 | Passes the copy protection, then types "look" commands in Laura's room: plain, with articles, a plural suffix and a pronoun (kParse, kSaid). |
| `parser-verbs.txt` | 7700 | Same start; verbs other than look, ending with one that makes Laura walk. |
| `parser-errors.txt` | 7700 | Same start; an unknown word (wordFail), broken sentences (syntaxFail), punctuation, upper case, a number and a dashed word. |
