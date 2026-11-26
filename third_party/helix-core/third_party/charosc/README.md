Character oscillators, ported from Twisted Genetics' own logue SDK units:

* `hoover_dsp.h`  — TwistedGenetics/TwistedGenetics-Hoover- (core/hoover_dsp.h)
* `dreadbass.h`   — TwistedGenetics/Dread-Bass (core/dreadbass.h)
* `rollers.h`     — TwistedGenetics/Rollers_Bass (core/rollers.h)

Only change: the fixed 48 kHz sample-rate constants became per-thread variables (`kSampleRate`, `setSampleRate`), so
they run at the host rate (and an offline print at another rate on another thread leaves the live voices alone). Used by helix-core/src/CharOsc.cpp, the only file that includes them.
