# helix-core in Paula's Revenge

Copied from TwistedGenetics/Twisted-Helix (Jamie's own synth). Changes made here:

- `third_party/dnfx/dn_fxc.c` is a stub (its effects give no wet signal) and the send limiter in
  `third_party/dnfx/dn_fx.c` (`dn_fx_limit`) was rewritten from scratch. The originals were ported from
  code under GPL-3.0 by other authors, which can't go into a closed-source app.
- `third_party/charosc/dreadbass.h`: GCC vector types are left out under MSVC (only the one-lane engine is used).
- `src/TableLibrary.cpp`: `requestLoadId` also takes `dir:<absolute path>`, a folder of single cycles loaded
  directly (Paula keeps your own wavetables by their paths).
