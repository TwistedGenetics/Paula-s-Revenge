# PAULA'S REVENGE

An 8-track, Amiga-style sample tracker for jungle, drum & bass and anything that wants that crunchy 8-bit Paula sound. Runs as a **standalone app** and as a **VST3 / AU instrument** in Ableton Live. Black and white, big pixel text, keyboard-driven.

It's an original program inspired by the classic Amiga trackers of the early 90s. It loads and saves ProTracker-style `.MOD` files and imports MED/OctaMED `MMD0`/`MMD1` modules.

![Paula's Revenge](docs/screenshot.png)

## Install (no building needed)

Every green build publishes a release. Open **Releases** on the right of the
repo page, download **PaulasRevenge-macOS.zip** from the newest one, and
follow the `INSTALL.txt` inside: copy the app into Applications and the
`.vst3` and `.component` into your plug-in folders, clear macOS quarantine,
rescan in Ableton. Builds are universal (Apple Silicon + Intel).

---

## The screen

| Area | What it does |
|---|---|
| Title bar | NEW, LOAD, SAVE (native `.prv`), SAVE MOD, RENDER WAV, text size `A-` / `A+` |
| Transport | Play song, play block, stop, edit mode, BPM, speed (ticks per line), octave, step |
| Sequence | The song order. Click a position to jump to it; INS / DEL entries; set which block plays there |
| Block | Pick, add, clone or delete blocks; set the number of lines (1–256) |
| Instruments | 63 slots. LOAD opens the sample browser, EDIT opens the sample editor. Volume, finetune, transpose, loop on/off, loop start/length. Drag the loop markers in the little waveform (double-click it to open the editor). Double-click a name to rename. Alt-click to preview |
| Pattern editor | 8 tracks. Click a track header to mute it, Alt/right-click to solo |
| Paula strip | Output character (see below), stereo separation, volume |
| Bottom row | 4 or 8 tracks, BIG VIEW (4 tracks at a time with large text), follow playback, hide/show panels, song name, host sync |

The app opens with the **dark** skin (cream on blue-black, coloured tracks, level bars in each track and a spectrum strip). The **DARK** button on the bottom row switches to black-on-white; **SPECTRUM** hides the strip.

## Sample browser

LOAD in the instrument panel opens a browser in place of the pattern. Every sample you highlight **plays straight away** (turn AUTO PLAY off to stop that), with its rate, length, mono/stereo and a waveform shown underneath.

| Key | Action |
|---|---|
| `↑` `↓` / `PgUp` `PgDn` | Move (and hear) the next sample |
| `→` / `Enter` on a folder | Open it · `←` or `Backspace` goes up |
| `Enter` / double-click | Load into the current instrument |
| `Space` | Play / stop |
| A letter | Jump to the next name starting with it |
| `Esc` | Close |

Shortcut buttons cover Home, Music, Desktop, Downloads, Documents and Drives; FINDER… opens the normal Mac file dialog.

## Sample editor

EDIT in the instrument panel (or double-click the little waveform) opens a full-size editor. Stereo samples show left and right separately.

* **Select:** drag in the waveform, Shift-click to extend, double-click or SEL ALL for everything. Click once to place the cursor (where PASTE inserts).
* **Zoom:** mouse wheel or ZOOM +/-, FIT SEL, FIT ALL. Click or drag the overview strip to move around. Zoomed right in, you see each sample as a step, the way Paula plays it.
* **DRAW:** draw the waveform with the mouse.
* **Edit:** CUT, COPY, PASTE, DELETE, CROP, SILENCE, REVERSE, MAX VOL (normalise), VOL −/+ (3 dB), FADE IN, FADE OUT, SET LOOP (loop = selection), TO MONO. Edits work on the selection, or the whole sample if nothing is selected. Loop points move with the audio when you cut or paste.
* **Play:** PLAY plays from the cursor (looping the sample's loop when LOOP is on), ▶ SEL plays just the selection.
* Keys: `Space` play/stop, `Cmd+X/C/V`, `Cmd+A`, `Delete`, `Cmd+Z` / `Cmd+Shift+Z`, `←` `→` scroll, `↑` `↓` zoom, `Esc` close.
* UNDO/REDO keep the last 40 edits per sample.

## Stereo samples

Stereo files stay stereo. A stereo sample plays with its own stereo image, centred, instead of following the Amiga left/right track layout (`8xx` still pans it). Mono samples behave exactly as before. Stereo is saved in `.prv` songs; SAVE MOD mixes stereo samples down to mono because the MOD format can't hold stereo. TO MONO in the sample editor converts one yourself.

**BIG VIEW** is on by default so the pattern text is large. Tab to track 5 and the view flips to tracks 5–8. Turn BIG VIEW off, or hide the panels, to see all 8 tracks at once.

## Keys

| Key | Action |
|---|---|
| `Z S X D C V G B H N J M` | Notes, lower octave (`, L . ; /` continue upwards) |
| `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P` | Notes, upper octave |
| `1` | Note off |
| `0–9 A–F` | Hex entry in the instrument / command columns |
| `Space` | Edit mode on/off (stops playback if playing) |
| `Enter` | Play block · `Shift+Enter` play song · `Cmd+Enter` play block from the cursor |
| `Esc` | Stop |
| Arrows | Move · `Shift+↑/↓` 4 lines · `Shift+←/→` track |
| `Tab` / `Shift+Tab` | Next / previous track |
| `PgUp` / `PgDn`, `Home` / `End` | Jump |
| `[` `]` | Octave down / up |
| `-` `=` | Previous / next instrument |
| `Delete` (⌫) | Clear the field under the cursor |
| `Shift+Delete` | Remove the line (pulls the track up) |
| `Alt+Delete` | Insert a blank line (pushes the track down) |
| `Cmd+C / X / V` | Copy / cut / paste track · add `Shift` for the whole block |
| `Cmd+↑ / ↓` | Transpose track a semitone · add `Shift` for an octave |
| `Cmd+Z` / `Cmd+Shift+Z` | Undo / redo |

A MIDI keyboard (or Ableton's MIDI) plays the current instrument, and records notes at the cursor when edit mode is on. MIDI 60 = `C-3`.

## Effect commands

Same numbering as ProTracker, so MOD files load straight in.

| Cmd | Effect |
|---|---|
| `0xy` | Arpeggio (+x, +y semitones) |
| `1xx` / `2xx` | Slide pitch up / down |
| `3xx` | Slide to note |
| `4xy` | Vibrato (speed x, depth y) |
| `5xy` | Slide to note + volume slide |
| `6xy` | Vibrato + volume slide |
| `7xy` | Tremolo |
| `8xx` | Pan (00 left – FF right) |
| `9xx` | Sample offset (xx × 256) |
| `Axy` | Volume slide (x up / y down) |
| `Bxx` | Jump to sequence position |
| `Cxx` | Set volume (00–40 hex) |
| `Dxx` | Next block, start at line xx (decimal) |
| `E0x` | LED filter: `E00` on, `E01` off |
| `E1x` / `E2x` | Fine slide up / down |
| `E4x` / `E7x` | Vibrato / tremolo waveform |
| `E5x` | Set finetune |
| `E6x` | Loop: `E60` set start, `E6x` repeat x times |
| `E8x` | Coarse pan |
| `E9x` | Retrigger every x ticks |
| `EAx` / `EBx` | Fine volume up / down |
| `ECx` | Cut note at tick x |
| `EDx` | Delay note by x ticks |
| `EEx` | Delay the line by x lines |
| `Fxx` | `01–1F` speed, `20–FF` BPM, `00` stop |

## The Paula sound

| Control | What it does |
|---|---|
| A500 | 4.4 kHz output filter of the original A500 — warm and dull |
| A1200 | Open output filter — brighter, more aliasing sparkle |
| CLEAN | No output filtering at all |
| STEPPED | Sample-and-hold playback like the real chip (off = smooth interpolation) |
| 8-BIT | Samples are played at 8-bit resolution |
| CRUNCH | In 8-track songs, track pairs are mixed together and squashed back to 8 bits, the way 8-channel Amiga trackers did it in software |
| LED | The "LED" lowpass filter, on at the start of the song (commands `E00`/`E01` switch it while playing) |
| SEP | Stereo separation from mono to hard Amiga left/right panning (tracks pan L-R-R-L) |

Samples are resampled on load so `C-3` plays them at their original pitch (16.5 kHz, the Amiga convention). For breaks, load the break, put `C-3` on line 0 and use `9xx` sample offsets to jump around it.

## In Ableton

* **HOST PLAY** — the song starts when Ableton starts and stops with it.
* **HOST BPM** — follows Ableton's tempo (at speed 6, one line = a 16th note). `Fxx` tempo changes in the song are ignored while this is on.
* The whole song, instruments and settings are saved inside your Live set.
* **RENDER WAV** writes the song once through as a 24-bit stereo WAV.

## Files

* `.prv` — Paula's Revenge song with samples (compressed).
* `.mod` — load 4/6/8-channel MODs (M.K., xCHN, 15-sample SoundTracker). SAVE MOD writes `M.K.` for 4 tracks or `8CHN` for 8 (64 lines per block, 31 instruments, 128 KB per sample — anything beyond that is reported in the status line).
* MED / OctaMED — `MMD0` and `MMD1` modules import with their blocks, sequence, samples, names and finetune. Synth/hybrid instruments and MED-only commands aren't supported; tempos of old 8-channel-mode songs are converted as closely as possible.

## Building

Actions does this on every push to `main`. Locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/prv_engine_test
```

The tracker engine (`Source/engine`) is plain C++ with no JUCE dependency; `Tests/EngineTest.cpp` checks rendering, pitch, MOD round-trips, MED import and loader robustness.
