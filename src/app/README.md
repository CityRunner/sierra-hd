# app

The program: window, input, presentation, configuration, saves and the
analysis cache. It is the only place (together with the audio output in
sci-audio) that uses the window system, OS files or threads, and it supplies
the libraries with their log sink, assertion handler and task-execution
interface.

Milestone 0 contains a command-line stub that reports its version and the
linked libraries; the window layer (SDL3) arrives at the end of milestone 3a.

## Invariants

- Libraries never reach the OS directly; app passes them data and services.
- Game files are opened read-only.
