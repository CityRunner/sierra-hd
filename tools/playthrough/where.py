#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints where The Colonel's Bequest is at the ticks of a globals file.

    where.py <globals file> [<checkpoints file>]

The globals file comes from sci-vm-trace --globals: per tick, the tick
requested, the tick reached and every global variable. Prints per tick the
room (global 11), the act (global 165: 0 is Act I) and the events of the act
witnessed so far (global 118, four bits; docs/playthrough.md).

With a checkpoints file (tests/playthrough/checkpoints.txt: lines of
`<name> <tick> <act> <room>`, the act as a Roman numeral and the room a
number or `-` for any), checks each checkpoint against the globals at its
tick and exits with 1 if one does not hold. The act `quit` checks the end
of the game instead: the game quit (global 4, the quit flag, set) before
the tick, in the room given.
"""

import pathlib
import sys

GLOBAL_QUIT = 4
GLOBAL_ROOM = 11
GLOBAL_EVENTS = 118
GLOBAL_ACT = 165
ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "ending"]


def act_name(value):
    return ROMAN[value] if 0 <= value < len(ROMAN) else str(value)


def read_globals(path):
    """{requested tick: (reached tick, room, act, events, quit)}"""
    states = {}
    for line in pathlib.Path(path).read_text().splitlines():
        fields = line.split()
        if len(fields) < 2 + GLOBAL_ACT + 1:
            continue
        values = fields[2:]

        def number(index):
            text = values[index]
            return int(text) if ":" not in text else -1

        states[int(fields[0])] = (int(fields[1]), number(GLOBAL_ROOM), number(GLOBAL_ACT), number(GLOBAL_EVENTS),
                                  number(GLOBAL_QUIT))
    return states


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    states = read_globals(sys.argv[1])
    for tick, (reached, room, act, events, quit_flag) in sorted(states.items()):
        ended = f", quit at tick {reached}" if quit_flag and reached < tick else ""
        print(f"tick {tick} (reached {reached}): room {room}, Act {act_name(act)}, events {events & 15:04b}{ended}")
    if len(sys.argv) == 2:
        return 0
    status = 0
    for line in pathlib.Path(sys.argv[2]).read_text().splitlines():
        fields = line.split("#", 1)[0].split()
        if not fields:
            continue
        name, tick, act, room = fields[0], int(fields[1]), fields[2], fields[3]
        if tick not in states:
            print(f"{name}: no globals at tick {tick}")
            status = 1
            continue
        reached, at_room, at_act, _, quit_flag = states[tick]
        if act == "quit":
            if not quit_flag or reached >= tick or (room != "-" and int(room) != at_room):
                print(f"{name}: tick {tick}: expected the game to have quit in room {room}, "
                      f"found room {at_room}, quit flag {quit_flag}, reached tick {reached}")
                status = 1
            else:
                print(f"{name}: tick {tick}: the game quit at tick {reached} in room {at_room}")
            continue
        if act_name(at_act) != act or (room != "-" and int(room) != at_room):
            print(f"{name}: tick {tick}: expected Act {act} in room {room}, "
                  f"found Act {act_name(at_act)} in room {at_room}")
            status = 1
        else:
            print(f"{name}: tick {tick}: Act {act} in room {at_room}")
    return status


if __name__ == "__main__":
    sys.exit(main())
