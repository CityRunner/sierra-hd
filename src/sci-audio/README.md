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
  play switch, a song's SysEx and reverb, a step after each music timer
  call) and asks of it (`PlayerChoices`: play mask, voices, rhythm
  channel). No device is the silent driver, with the AdLib's choices.
- `sci/audio/adlib.hpp`: `AdlibDriver`, ScummVM's SCI0 AdLib driver (SCI0
  late) on an `Opl3` in dual OPL2 mode (stereo panning): voice mapping from
  the sound header, round-robin voice allocation, patches, pitch wheel,
  velocity and master volume. `load_adlib_patches` reads patch.003 (or the
  bank in an early ADL.DRV) through sci-resource. Milestone 7.

- `sci/audio/mt32.hpp`: `Mt32`, a Roland MT-32 or CM-32L on mt32emu
  (`third_party/mt32emu`, LGPL 2.1 or later): opened on the user's control
  and PCM ROMs, passed in as bytes (app reads the files; sci-audio does no
  file I/O), which mt32emu identifies (`roms()`: model, ROM names and
  versions); MIDI messages and SysEx at once or at a frame; stereo int16
  output at the native rate of 32000 Hz. Wrong, unknown or mismatched ROMs
  are an error, not an abort. `identify_mt32_rom` tells what one file is
  (for choosing among the files of a ROM folder). Milestone 7.
- `sci/audio/mt32_driver.hpp`: `Mt32Driver`, ScummVM's SCI0 MT-32 driver
  (`MidiPlayer_Midi` on an emulated MT-32, SCI0 late) on an `Mt32`: at
  open the reset and the game's patch.001 (`parse_mt32_patch`,
  `load_mt32_patch`: display texts, master volume, patches 1-96, timbres,
  rhythm key map, partial reserve, reverb settings) as SysEx written into
  the synthesiser; per song the part assignment (initTrack) and the
  default reverb; MIDI with the driver's program, volume and rhythm
  handling. It reports play mask 0x01, 32 voices and the rhythm channel
  9. Milestone 7.

- `sci/audio/mixer.hpp`: `Mixer`, the software mixer: the device's stereo
  output at its rate (49716 Hz for the OPL, 32000 Hz for the MT-32) to the
  output device's rate, by 4-point
  cubic Hermite interpolation, with a volume, clipping to 16 bits and a
  small speed correction (`queue_speed`) that holds the output queue near
  its target as the clocks drift. Doubles, no threads, no I/O. Milestone 7.

The audio output itself (SDL3, the hand-off to the audio thread) is in app
(`src/app/src/audio_output.cpp`), which owns the window-system dependency.

Rendering a sound resource through the player and the driver is
`sci::vm::render_sound` (AdLib or MT-32; sci-vm, as the player is there),
and the tool `tools/sound-render` (`--device mt32 --roms <folder>` for the
MT-32). Reading the ROM folder and the fallback to the AdLib are in app's
platform layer (`sci/platform/mt32_roms.hpp`).

## Invariants

- Sound timing is game logic: signals and cues match ScummVM (milestones 4 and 7).
- Only the audio-output part may use threads or OS facilities; the OPL core output is deterministic.
- The OPL core is integer-only with all state in the object; its output at
  the native rate is what the golden hashes check
  (`tests/unit/audio/opl3_test.cpp`). Resampling to the device rate is
  outside the hashes.
- The mixer may use floating point (double) and need not be bit-exact: its
  output is never hashed (spec section 8).
- The MT-32 synthesiser (mt32emu) is not hashed and need not be
  deterministic across platforms: its renderer is integer-only, but a pitch
  jitter comes from the C library's `rand()`, so its output is a function
  of the ROMs, the input and `rand()`'s state (docs/ASSUMPTIONS.md,
  "Milestone 7: mt32emu").
- The AdLib driver is integer-only and deterministic too; the player's
  timing, signals and cues are the same with it as with the silent driver
  (`tests/unit/vm/music_device_test.cpp`).
- The MT-32 driver changes which channels play and the voices kDoSound
  GetPolyphony reports (32), not the timing, signals or cues
  (docs/ASSUMPTIONS.md, "Milestone 7: the MT-32 driver").

## MT-32 ROMs in the tests

The tests of `Mt32` and `Mt32Driver` (`tests/unit/audio/mt32_test.cpp`,
`mt32_driver_test.cpp`) and of the ROM folder
(`tests/unit/platform/mt32_roms_test.cpp`) that need ROMs read
them from the folder in `SIERRA_HD_MT32_ROMS`, and pass with a "skipped"
note when it is not set or a file is missing (CI has no ROMs). Files:
`MT32_CONTROL.1987-10-07.v1.07.ROM` (or `MT32_CONTROL.ROM`),
`MT32_PCM.ROM`, `CM32L_CONTROL.1989-12-05.v1.02.ROM` (or
`CM32L_CONTROL.ROM`) and `CM32L_PCM.ROM`. In the container the folder is
mounted read-only:

    SIERRA_HD_MOUNTS=/path/to/roms:/roms docker/run.sh \
        env SIERRA_HD_MT32_ROMS=/roms build/debug/tests/unit/sci-audio-tests

The ROMs are Roland's and never committed or uploaded.
