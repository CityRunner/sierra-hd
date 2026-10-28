# Milestone 7: sound

Status: **awaiting approval**.

Exit criteria (spec section 12): golden hashes of the OPL output for a
fixed set of sound resources; every sound resource plays without errors on
both devices; sound signals and cues match ScummVM; a by-ear comparison
with ScummVM is recorded in the report.

The decisions are in `docs/ASSUMPTIONS.md`, in the sections "Milestone 7:
..." (the OPL core, the AdLib driver, the OPL comparison and the sound
golden hashes, mixer and audio output, mt32emu, the MT-32 driver, the MT-32
comparison, gaps in the music) and "MT-32 ROMs (2026-10-28)".

## Changes of plan during this milestone

- **Milestone SU (sprite undithering, addendum A2) is dropped.** The
  customer first put it on hold (2026-10-23), then dropped it completely
  (2026-10-28). A2 is marked withdrawn in `docs/SPEC_ADDENDA.md`. Sprites
  stay the original cels scaled N x N. The milestone 9 playthrough is done
  without undithering. The unmerged SU work was deleted.
- **Small agent tasks** (`docs/ASSUMPTIONS.md`, 2026-10-23). The milestone
  was built as ten tasks of about an hour each. Long checks run in
  resumable pieces. This kept the stops at usage limits short.

## What was implemented

1. **The OPL core.** Nuked OPL3 (LGPL 2.1+) is vendored at a pinned commit
   and wrapped as `sci::audio::Opl3`.
   - The core uses no floating point and keeps no shared state.
   - Golden hashes are taken at the chip's native rate, 49,716 Hz.
2. **The AdLib driver.** ScummVM's SCI0 AdLib driver is ported into
   sci-audio, under the existing music player in sci-vm. It covers:
   - instruments from patch.003;
   - voice allocation;
   - pitch bend, velocity and volume;
   - the dual OPL2 stereo setup.

   The player's timing, signals and cues are unchanged. The silent driver
   and the AdLib driver report the same values to the player.
3. **The MT-32.**
   - **Emulator:** mt32emu 2.8.3 (LGPL 2.1+) is vendored and wrapped as
     `sci::audio::Mt32`.
   - **Driver:** ScummVM's SCI0 MT-32 driver is ported. At open it uploads
     the timbres, patches, rhythm setup and reverb from patch.001, and shows
     the game's display texts.
   - **Player:** it asks the device for its play mask, polyphony and rhythm
     channel, as ScummVM's player does.
   - **ROMs:** the configuration names a ROM folder (`mt32_rom`). The
     program identifies the ROMs by content and prefers MT-32 1.07, then
     CM-32L. Without valid ROMs it falls back to AdLib and logs a warning.
   - **Where ROMs live:** they stay on the local machine only. CI runs
     without them, and ROM tests skip there.
4. **Audio output in the program.**
   - **Threads:** the VM, the driver, the synthesiser and the mixer run on
     the main thread. SDL3's audio thread only plays an `SDL_AudioStream`.
   - **Resampling:** a 4-point cubic resampler with a small speed
     correction keeps the queue at about 70 ms.
   - **Configuration:** `music = adlib | mt32 | off` and `volume = 0..100`.
   - **No device:** without an audio device the game runs on the silent
     driver, with the same timing.
5. **No gaps in the music.** Three causes were measured and fixed:
   - **HD pictures** are now made on a background thread. A room's previous
     frame stays up to 1 s, then the picture shows at 1x until its HD image
     is ready.
   - **The renderer** is set up before the music starts.
   - **The queue** is filled again after an underrun.

   Nothing the interpreter sees depends on the HD images.
6. **Comparisons with ScummVM** (the reference, patch 0003):
   - an OPL register log (`SIERRA_HD_OPL`), the seventh comparison of every
     scenario on AdLib;
   - a MIDI log (`SIERRA_HD_MIDI`), compared instead when a scenario runs
     on the MT-32 (`SIERRA_HD_MUSIC=mt32`).

   Both logs are keyed by music timer call. In deterministic mode the
   reference ages its AdLib notes on the virtual clock, as ours does.
7. **Tools.**
   - `sci-sound-render` renders any sound on either device and writes or
     checks the golden hashes.
   - `tools/audio-gaps/find_gaps.py` finds underruns in a capture.

## Results

### Scenarios: both devices, seven comparisons

All 33 runs of `tests/scenarios/scenarios.txt` were run in the reference
and in sci-vm twice, once on each device. The runs were in the main
checkout after the last merge, with the reference rebuilt from the current
patch.
- **AdLib (66 runs in all):** traces, screens, maps, actors, parser,
  layout and the OPL register log all match.
- **MT-32:** the same, with the MIDI log in place of the OPL log.

| Scenario | Stop tick | Seed | Kernel calls | OPL writes (AdLib) | MIDI messages (MT-32) |
|---|---|---|---|---|---|
| boot | 3,600 | 0 | 12,022 | 1,286 | 417 |
| boot | 3,600 | 7 | 12,022 | 1,286 | 417 |
| boot | 3,600 | 12345 | 12,022 | 1,286 | 417 |
| boot | 18,000 | 0 | 57,622 | 6,008 | 1,476 |
| mouse | 3,600 | 0 | 13,864 | 710 | 288 |
| text | 3,600 | 0 | 34,624 | 1,286 | 417 |
| copy-protection | 6,000 | 0 | 57,406 | 2,048 | 612 |
| menu-keys | 3,600 | 0 | 29,745 | 1,286 | 417 |
| intro | 30,000 | 0 | 227,303 | 81,604 | 14,638 |
| mansion-walk | 11,000 | 0 | 81,728 | 1,666 | 2,067 |
| menus | 9,030 | 0 | 67,952 | 1,040 | 1,183 |
| bedroom-wait | 20,000 | 0 | 119,178 | 1,040 | 1,165 |
| parser-look | 7,700 | 0 | 74,717 | 1,160 | 1,202 |
| parser-verbs | 7,700 | 0 | 73,547 | 1,160 | 1,202 |
| parser-errors | 7,700 | 0 | 73,875 | 1,160 | 1,202 |
| mouse-walk | 12,600 | 0 | 109,769 | 2,958 | 2,980 |
| grounds | 12,600 | 0 | 110,283 | 4,698 | 2,773 |
| inventory-and-actions | 24,000 | 0 | 226,195 | 5,888 | 4,020 |
| boot-and-bedroom | 5,000 | 7 | 39,019 | 1,160 | 1,180 |
| walls-keyboard | 9,300 | 0 | 61,779 | 1,040 | 1,165 |
| mouse-obstacles | 8,500 | 0 | 56,963 | 1,040 | 1,165 |
| stairs | 8,200 | 0 | 59,428 | 1,468 | 1,823 |
| lillian-walk | 17,400 | 0 | 107,135 | 1,040 | 1,165 |
| attic | 9,400 | 0 | 76,231 | 1,040 | 2,983 |
| parser-forms | 15,122 | 0 | 249,195 | 1,160 | 1,202 |
| parser-forms-2 | 19,555 | 0 | 305,534 | 4,206 | 4,791 |
| parser-words-hallway | 39,821 | 0 | 785,671 | 1,040 | 1,165 |
| parser-words-garden | 39,854 | 0 | 715,093 | 15,246 | 5,437 |
| parser-words-kitchen | 39,803 | 0 | 647,968 | 5,672 | 3,879 |
| parser-words-bedroom | 24,024 | 0 | 495,499 | 1,160 | 1,202 |
| parser-words-downstairs | 22,057 | 0 | 378,735 | 4,870 | 5,514 |
| menus-mouse | 8,700 | 0 | 78,564 | 1,040 | 1,165 |
| menus-keys-2 | 9,500 | 0 | 81,649 | 1,040 | 1,183 |

- **Totals:** 159,792 OPL register writes and 71,912 MIDI messages, all
  identical to ScummVM's.
- **Signals and cues match ScummVM** on both devices. The kernel-call
  traces, which carry every kDoSound call with the signals and cues the
  scripts read, are identical in both engines.
  - They are also identical between the two devices, call for call.
  - The game calls GetPolyphony, the one value that differs by device (9
    or 32), only once, in script 16, and no scenario reaches that call.
    Both engines return the same value on each device.
- **No port bug turned up.**
- **The comparisons catch differences.** Changing one value in an OPL log,
  or one byte in a MIDI log, fails the comparison at that line.

### Every sound plays without errors on both devices

All 119 sound resources were rendered on AdLib and on the MT-32 (MT-32
1.07 ROMs), with no error or warning from the drivers or the synthesisers.
- **Signals and cues:** for every sound, the signals, the cues and the
  timer call at which the song ends are identical on both devices.
- **Sound 128 is silent on the MT-32.** Its only channel is 14, and the
  SCI0 MT-32 driver plays channels 1–9 only, as Sierra's driver and
  ScummVM's do. On the AdLib it plays.

### Golden hashes of the OPL output

`tests/golden/sounds.txt` holds the SHA-256 of the OPL samples at the
native rate for all 119 sounds: the first 20 s of each, or until the song
ends, which 84 do sooner.
- **Check:** `sci-sound-render <game> --golden tests/golden/sounds.txt
  --check`.
- **Results:** all match on x86-64 (release and debug) and on ARM64 under
  qemu-user. They were checked again after the MT-32 driver was merged.
- **Synthetic note:** a unit-test note has a golden hash too. It is the
  same on x86-64, ASan+UBSan and ARM64, and CI checks it without the game.

### The by-ear comparison with ScummVM

Captures of the title music running into the intro's first room, about 70
s each, 44.1 kHz stereo. They are in `reports/images/audio/` (git-ignored:
they are derived from the game's music):

| File | Engine | Device |
|---|---|---|
| `m7-title-music-adlib-nogaps.wav` | ours, release build | AdLib |
| `m7-title-music-adlib-scummvm.wav` | ScummVM (the reference) | AdLib |
| `m7-title-music-mt32-nogaps.wav` | ours, release build | MT-32, ROM 1.07 |
| `m7-title-music-mt32-scummvm.wav` | ScummVM (the reference) | MT-32, the same ROMs |

- **How they were made:** headless, with SDL's disk audio driver.
  ScummVM ran in its normal mode (not deterministic) with
  `--music-driver=adlib` or `mt32`.
- **Gaps:** `find_gaps.py` finds none in either ScummVM capture, and none
  in our AdLib capture. Our MT-32 capture has one short cut at 35.9 s.
  That comes from the disk driver's clock, which runs about 0.5 % fast
  (`docs/ASSUMPTIONS.md`, "Milestone 7: gaps in the music"); a sound card
  is within about 0.01 %.
- **What the comparison already shows.** By construction, the register
  writes and MIDI messages are identical, so the captures can differ only
  below the driver:
  - the OPL emulator build (ScummVM carries an older fork of Nuked OPL3);
  - the mt32emu version (ScummVM carries 2.7.0, we use 2.8.3);
  - resampling and output levels;
  - the order of SysEx and notes inside one timer call on the MT-32, where
    ScummVM leaves a few old notes sounding at four song changes of the
    intro (documented in ASSUMPTIONS).
- **Listening.** This machine has no sound card, so the listening itself
  is the customer's (the spec: "otherwise the customer does the
  listening check at milestone 7"). **The result is to be recorded here.**

## Tests and style

- **Local checks.** `scripts/pre-push.sh` passes on the merged branch, run
  step by step (the rule of small tasks):
  - clang-format, clang-tidy, the repository policy and the hint check;
  - all test suites in the release, ASan+UBSan and TSan builds;
  - fuzzing;
  - all tests on ARM64 under qemu-user.
- **New unit tests:**
  - **OPL core:** silence, key-on and release, write timing, independent
    chips, reset, block sizes, a golden note.
  - **AdLib driver:** the exact register sequences for note on and off,
    voice allocation and stealing, pitch wheel, volume, velocity and pan,
    patch bank formats.
  - **Player with a device:** queued kDoSound messages, and identical
    signals and cues with the silent, AdLib and MT-32 devices.
  - **Mixer:** rate conversion at 22,050–96,000 Hz, levels, clipping, the
    speed correction.
  - **Audio output:** the hand-off to SDL's thread, under TSan; the top-up
    after an underrun.
  - **Background HD pictures:** under TSan.
  - **mt32emu wrapper and MT-32 driver,** with ROMs locally and skipped in
    CI:
    - ROM identity, rendering, SysEx and volume;
    - the SysEx sent at open and close, routing, part assignment, reverb.
  - **ROM choice and fallback to AdLib.**

## CI minutes

CI_MINUTES

## Deviations from the spec

None from section 2.
- **Not bit-exact:** mt32emu's output depends on the C library's `rand()`
  (a small pitch jitter), so MT-32 output is repeatable only after the
  same `srand()`. The spec requires determinism only for the OPL core. The
  program seeds `rand()` once; its own code never calls it.
- **Ordering of SysEx and notes:** our MT-32 path keeps the order the
  driver sends, as a real MT-32 receives it. ScummVM reorders SysEx ahead
  of the notes sent before it in the same timer call. The logs at the
  driver match either way.

## Open questions

1. **The by-ear comparison.** Please listen to the four captures and tell
   me what you hear: tempo, instruments, balance and anything missing or
   wrong. I will record it here.
2. **Showing a room while its HD picture is made.** The previous frame
   stays up to 1 s, then the room shows at 1x until the HD image is
   ready. In a release build this takes 14–16 ms, so it is seen only on a
   slow machine. Is this acceptable, or should the 1x picture show at once
   (a short blocky flash at room entry)?
3. **The patched MT-32 control ROM** in your archive
   (`...vX.XX-patched.ROM`) is not known to mt32emu, so it is skipped. The
   program uses the 1.07 ROM and the CM-32L set. Is that what you want?
