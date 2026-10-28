# audio-gaps

Finds the gaps (underruns) in a capture of the program's audio output, as
SDL's disk driver writes it:

    SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=capture.raw xvfb-run -a build/release/src/app/sierra-hd
    tools/audio-gaps/find_gaps.py capture.raw [--rate 44100] [--wav capture.wav]

After the music starts it lists the runs of at least 1 ms of exact zeros
that cut into the waveform (an underrun; a natural silence fades out
first) and the single steps much larger than those around them (dropped
audio), and exits 1 if there are any. `--wav` also writes the capture as a
WAV file.

The disk driver paces itself with `SDL_Delay` in whole milliseconds, so its
clock is off by a few tenths of a per cent depending on the buffer size
(1024 frames at 44100 Hz: about 0.5 % fast), more than the program's speed
correction of 0.2 % absorbs; long captures then drift to an empty or a full
queue. `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=1020` came closest here
(docs/ASSUMPTIONS.md, "Milestone 7: gaps in the music").
