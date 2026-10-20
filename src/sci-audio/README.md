# sci-audio

Sound resource playback: the silent driver (timing, signals and cues), the AdLib and MT-32 devices, and the mixer.

Milestone 0 contains only the library skeleton; the content arrives with the
milestones named below.

## Invariants

- Sound timing is game logic: signals and cues match ScummVM (milestones 4 and 7).
- Only the audio-output part may use threads or OS facilities; the OPL core output is deterministic.
