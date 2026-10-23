# sci-audio

Sound resource playback: the silent driver (timing, signals and cues), the AdLib and MT-32 devices, and the mixer.

The music player itself is in sci-vm (docs/ASSUMPTIONS.md, "Sound"); the
device layer under it is here (milestone 7).

## Contents

- `sci/audio/opl3.hpp`: `Opl3`, one OPL3 chip on Nuked OPL3
  (`third_party/nuked-opl3`, LGPL 2.1 or later): reset, register writes
  (with the chip's bus delay, or immediate), and stereo int16 output at the
  native rate of 49716 Hz. In OPL2 mode, where it starts, it is the AdLib's
  chip. Milestone 7.
- `sci/audio/midi_device.hpp`: `MidiDevice`, what the SCI0 player sends to
  a music device (MIDI messages, the track header, the global volume, the
  play switch, a step after each music timer call). No device is the
  silent driver.
- `sci/audio/adlib.hpp`: `AdlibDriver`, ScummVM's SCI0 AdLib driver (SCI0
  late) on an `Opl3` in dual OPL2 mode (stereo panning): voice mapping from
  the sound header, round-robin voice allocation, patches, pitch wheel,
  velocity and master volume. `load_adlib_patches` reads patch.003 (or the
  bank in an early ADL.DRV) through sci-resource. Milestone 7.

Rendering a sound resource through the player and the driver is
`sci::vm::render_sound` (sci-vm, as the player is there), and the tool
`tools/sound-render`.

## Invariants

- Sound timing is game logic: signals and cues match ScummVM (milestones 4 and 7).
- Only the audio-output part may use threads or OS facilities; the OPL core output is deterministic.
- The OPL core is integer-only with all state in the object; its output at
  the native rate is what the golden hashes check
  (`tests/unit/audio/opl3_test.cpp`). Resampling to the device rate is
  outside the hashes.
- The AdLib driver is integer-only and deterministic too; the player's
  timing, signals and cues are the same with it as with the silent driver
  (`tests/unit/vm/music_device_test.cpp`).
