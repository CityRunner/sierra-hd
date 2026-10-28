#!/usr/bin/env python3
"""Finds the gaps (underruns) in a capture of the program's audio output.

    find_gaps.py <capture.raw> [--rate 44100] [--wav <out.wav>]

The capture is signed 16-bit little-endian stereo, as SDL's disk audio
driver writes it (SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=<file>).
After the music starts (the first sample that is not zero) it lists:

- gaps: runs of at least 1 ms in which both channels are exactly zero and
  which cut into the waveform (the frame before the run is louder than
  64): what an underrun sounds like. A natural silence fades out to zero
  first and is not counted.
- jumps: single steps much larger than the steps around them (more than
  1500 and over 8 times the largest of the two steps on either side), as
  dropped audio leaves them.

With --wav the capture is also written as a WAV file. Exits 1 if it finds
a gap or a jump, 0 otherwise.
"""

import argparse
import sys
import wave
from array import array

MIN_GAP_MS = 1
EDGE = 64
JUMP = 1500
JUMP_RATIO = 8


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture")
    parser.add_argument("--rate", type=int, default=44100)
    parser.add_argument("--wav")
    args = parser.parse_args()

    with open(args.capture, "rb") as file:
        data = file.read()
    data = data[: len(data) // 4 * 4]
    samples = array("h")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    left = samples[0::2]
    right = samples[1::2]
    frames = len(left)
    rate = args.rate

    if args.wav:
        with wave.open(args.wav, "wb") as out:
            out.setnchannels(2)
            out.setsampwidth(2)
            out.setframerate(rate)
            out.writeframes(data)

    start = next((i for i in range(frames) if left[i] != 0 or right[i] != 0), None)
    print(f"{frames / rate:.2f} s captured", end="")
    if start is None:
        print(", all silent")
        return 0
    print(f", music from {start / rate:.3f} s")

    gaps = []
    min_gap = rate * MIN_GAP_MS // 1000
    i = start
    while i < frames:
        if left[i] == 0 and right[i] == 0:
            j = i
            while j < frames and left[j] == 0 and right[j] == 0:
                j += 1
            before = max(abs(left[i - 1]), abs(right[i - 1]))
            if j - i >= min_gap and before > EDGE and j < frames:
                gaps.append((i, j - i, before))
            i = j
        else:
            i += 1

    jumps = []
    for channel in (left, right):
        steps = [abs(channel[k + 1] - channel[k]) for k in range(start, frames - 1)]
        for k in range(2, len(steps) - 2):
            step = steps[k]
            if step > JUMP:
                around = max(steps[k - 2], steps[k - 1], steps[k + 1], steps[k + 2])
                if step > JUMP_RATIO * around:
                    jumps.append(start + k + 1)
    # The edges of a gap are steps too; they are counted with the gap.
    edges = {edge for frame, length, _ in gaps for edge in (frame, frame + length)}
    jumps = sorted(k for k in set(jumps) if not any(abs(k - edge) <= 2 for edge in edges))

    for frame, length, before in gaps:
        print(f"gap at {frame / rate:.3f} s: {length * 1000 / rate:.1f} ms (cut at level {before})")
    for frame in jumps:
        print(f"jump at {frame / rate:.3f} s")
    print(f"{len(gaps)} gaps, {len(jumps)} jumps")
    return 1 if gaps or jumps else 0


if __name__ == "__main__":
    sys.exit(main())
