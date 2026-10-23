# sci-audio

Sound resource playback: the silent driver (timing, signals and cues), the AdLib and MT-32 devices, and the mixer.

The music player itself is in sci-vm (docs/ASSUMPTIONS.md, "Sound"); the
device layer under it arrives here with milestone 7.

## Contents

- `sci/audio/opl3.hpp`: `Opl3`, one OPL3 chip on Nuked OPL3
  (`third_party/nuked-opl3`, LGPL 2.1 or later): reset, register writes
  (with the chip's bus delay, or immediate), and stereo int16 output at the
  native rate of 49716 Hz. In OPL2 mode, where it starts, it is the AdLib's
  chip. Milestone 7.

## Invariants

- Sound timing is game logic: signals and cues match ScummVM (milestones 4 and 7).
- Only the audio-output part may use threads or OS facilities; the OPL core output is deterministic.
- The OPL core is integer-only with all state in the object; its output at
  the native rate is what the golden hashes check
  (`tests/unit/audio/opl3_test.cpp`). Resampling to the device rate is
  outside the hashes.
