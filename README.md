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
| Title bar | NEW, LOAD, SAVE (native `.prv`), SAVE MOD, RENDER WAV, EXPORT SAMPLES, text size `A-` / `A+` |
| Transport | Play song, play block, stop, edit mode, BPM, speed (ticks per line), octave, step |
| Sequence | The song order, with a scrollbar. Click a position to jump to it. INS / DEL entries, set which block plays there. ▲ ▼ (or `Alt+↑/↓` in the pattern, or drag a row) move the selected position. CLONE IN PLACE gives this position its own copy of the block (see below) |
| Block | Pick, add, clone or delete blocks; set the number of lines (1–256). NUM − / NUM + swap the block's number with its neighbour (the song still sounds the same) |
| Instruments | 63 slots with a scrollbar. LOAD opens the sample browser, EDIT the sample editor, PARAMS the envelope/filter/pan page. Volume, finetune, transpose, loop on/off, loop start/length. Drag the loop markers in the little waveform (double-click it to open the editor). Double-click a name to rename. Alt-click to preview |
| Pattern editor | 8 tracks. Click a track name to mute it, Alt/right-click to solo. Drag in the pan bar under the name to pan that track; double-click it to go back to AUTO |
| Paula strip | Output character (see below), stereo separation, volume, and an L/R output meter |
| Bottom row | 4 or 8 tracks, BIG VIEW (4 tracks at a time with large text), follow playback, hide/show panels, song name, host sync |

The app opens with the **dark** skin (cream on blue-black, coloured tracks, level bars in each track and a spectrum strip). The **DARK** button on the bottom row switches to black-on-white; **SPECTRUM** hides the strip.

## Panning and left/right balance

Every track comes out of both speakers by default. How a track is panned, in order:

1. **`8xx`** in the pattern (`800` hard left, `880` centre, `8FF` hard right). This is an absolute position now, and isn't scaled by SEP.
2. **The track's pan** (the bar under each track name). **AUTO** means "no track pan".
3. **The instrument's pan** (PARAMS page), when its AMIGA L/R button is off.
4. Otherwise **Amiga style**: tracks 1 and 4 lean left, 2 and 3 lean right, by the **SEP** amount (20% by default, 0% = all centred). Stereo samples play centred with their own stereo image.

The **L / R meter** on the right of the Paula strip shows the balance. The plugin and app only run in stereo, and the standalone app turns on both of your interface's first two outputs if an old setup had only one.

## Instrument parameters (PARAMS)

PARAMS in the instrument panel opens a page for the current instrument. Everything is off or neutral by default, so MODs and older songs sound exactly as before.

* **Volume envelope**: VOL ENVELOPE on/off, ATK, DEC, SUS, REL (1 ms – 10 s), with a picture of the shape. With the envelope on, `OFF` in the pattern (or letting go of a key) starts the release instead of cutting the note.
* **Filter**: OFF / LOW / HIGH / BAND pass, CUT (20 Hz – 20 kHz), RES, AMT (how far the filter envelope moves the cutoff, ±8 octaves), KEY (how much the cutoff follows the note).
* **Filter envelope**: ATK, DEC, SUS, REL.
* **Pan**: AMIGA L/R follows the track layout above; switch it off and use PAN to give the instrument its own position.
* **HOLD TO PLAY**, or play the computer keyboard, to hear it. RESET ALL puts everything back. Double-click any slider for its default.

Envelopes and filters run per note on top of the Paula sound. They're saved in `.prv` songs and Live sets. A MOD can't hold them (SAVE MOD says so).

## Arrange page

**ARRANGE** in the sequence panel opens the song order full size, each position with a picture of its block's notes:

* Click a position, Shift-click to select a range, then drag it to a new place (a red line shows where it will land).
* ▲ MOVE / ▼ MOVE (or `Alt+↑/↓`), COPY, CUT, PASTE, REPEAT (`Cmd+D`, duplicates the selection after itself), DELETE.
* INSERT adds the BLOCK number after the cursor; SET makes the selected positions play that block.
* `Cmd+Z` / `Cmd+Shift+Z` undo and redo; double-click a position or press Enter to go back to the pattern.

## Sequence tools

* **Move positions**: select one and press ▲ / ▼, use `Alt+↑` / `Alt+↓` in the pattern, or drag it in the list.
* **CLONE IN PLACE**: if position 1 plays block 2, CLONE IN PLACE copies block 2 into a new block 3, moves every later block up one number (the sequence follows, so nothing else changes), and points position 1 at the new block 3. The song order keeps the same length; only one new block number exists. Now you can change it without touching the other places block 2 is used. The status line says exactly what happened.
* **NUM − / NUM +** (Block panel): swap the current block's number with the one before/after.
* `Cmd+Z` / `Cmd+Shift+Z` in the pattern undo and redo all of these.

## Saving and your samples

Songs are self-contained: SAVE (`.prv`) stores every sample inside the song, stereo included, and a Live set stores the whole song with its samples. You can delete or move the original WAVs. **EXPORT SAMPLES** writes every instrument that has a sample to a folder you choose as `01 NAME.wav`, `02 NAME.wav`… (16-bit, stereo kept, at the rate that plays them at C-3).

## The standalone app window

The app uses the normal Mac window: **red** quits, **yellow** minimises to the Dock, **green** goes full screen. The menus have **Audio/MIDI Settings…** (`Cmd+,`), **Toggle Full Screen** (`Ctrl+Cmd+F`) and **Quit** (`Cmd+Q`). The app remembers its window size and position.

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

## Sample rates

Each instrument has a **RATE**: the speed (Hz) it plays at on C-3. Changing it leaves the sample data alone, so a lower rate plays lower and slower. In the sample editor: RATE (drag, use +/−, or double-click to type), PRESET steps through 8363 / 11025 / 16574 / 22050 / 32000 / 44100 / 48000. **RESAMPLE** converts the data to the TO rate while keeping the pitch: use a low rate (11025, 8000…) for lo-fi grit, with **RAW** for no anti-alias filtering, the rough old-sampler way. In the browser, **KEEP RATE** loads a file at its own rate instead of converting it to the Amiga rate (16574 Hz). The rate is shown on the instrument's waveform, saved in songs, used for EXPORT SAMPLES, and converted for SAVE MOD.

## Time stretch (Akai style)

**STRETCH** in the sample editor opens the old-sampler time stretch, the warbly, metallic jungle stretch, for the selection or the whole sample:

* **CLASSIC**: the Akai sound, matched sample-for-sample to recordings of the real thing. The output is built from fixed-length cycles. Each cycle starts reading the source at the point the output has reached (so slices repeat when stretching and get skipped when squashing), and it fades in over two-thirds of the cycle while the previous slice carries on and fades out. The long, linear blend gives the smooth, phasey, metallic buzz of 90s jungle stretches.
* **REVISED**: cleaner. Each join is lined up by waveform shape (within 5% of a cycle) and crossfaded over 30% of a cycle.
* **HARD**: no lining up and no blend, so every join can click. Rougher than real hardware.
* **TIME %** (25–2000%), **SEMI** and **CENTS** (pitch, independent of time), **CYCLE** (20–2000, default 1000; in 44.1 kHz samples like Akaizer, converted for the sample's own rate). A shorter cycle gives a faster, more metallic buzz; a longer one gives more robotic, echoey repeats.
* **FIT TO LINES** sets TIME % so the selection lasts that many lines at the song's speed and BPM.
* **PREVIEW** to hear it, **APPLY** to keep it (undoable).

The controls follow Akaizer (CLASSIC/REVISED, time factor, cycle length, transpose). The Akai firmware isn't open, so CLASSIC was measured instead: an Amen break and recordings of it stretched ×2 and ×4 on the real unit (one played back +12 and +24 semitones). Tracing every slice of the recordings back to the original showed fixed cycles, a constant jump back at every join (no searching for a better join point), and a straight-line crossfade lasting 2/3 of the cycle. Rendered with the same settings, CLASSIC matches those recordings to within 19–24 dB, about as close as the recording itself allows; the old splice methods managed 2–5 dB.

## Sample editor

EDIT in the instrument panel (or double-click the little waveform) opens a full-size editor. Stereo samples show left and right separately.

* **Select:** drag in the waveform, Shift-click to extend, double-click or SEL ALL for everything. Click once to place the cursor (where PASTE inserts). **Grab either end of a selection** (the pointer changes to ↔) to move just that end; clicking inside a selection keeps it.
* **Play it from the keyboard:** the computer keys (same layout as the pattern, `[` `]` for octave) and a MIDI keyboard play the sample at the key's pitch through the full instrument sound, several notes at once, with a playhead for each. **SEL KEYS** makes them play only the selection (looped when LOOP is on).
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
| `8xx` | Pan (00 left – 80 centre – FF right) |
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

### Extra commands (G–Z)

Inspired by the Polyend Tracker's step FX. Type the letter in an effect column, then two hex digits. Put the cursor on any command to see what it does in the help strip under the pattern.

| Cmd | Effect |
|---|---|
| `Gxx` | Glide to the note over xx ticks |
| `Hxx` | High-pass filter, cutoff xx (`00` 20 Hz – `FF` 20 kHz) |
| `Kxx` | Chance: the step plays xx/FF of the time |
| `Lxx` | Low-pass filter, cutoff xx |
| `Mxx` | Micro-move: `01–7F` late by xx/128 of a line, `81–FF` early by (xx−80)/128 (early works inside a block, not across into the next) |
| `Nxx` | Note length: release after xx ticks |
| `Oxx` | Overdrive amount |
| `Pxx` | Start at xx/256 of the sample |
| `Qxx` | Filter resonance |
| `Rxy` | Roll: retrigger every x ticks; y `1–7` quieter, `9–F` louder each time |
| `Sxx` | Play slice `0–F` of 16 equal slices (stops at the slice end) |
| `Txx` | Micro-tune: `80` = 0, each step = 1 cent |
| `U01` / `U00` | Play backwards / forwards |
| `Wxx` | Band-pass filter, cutoff xx |
| `Xxy` | Randomise: x `0` note ±y semitones, `1` volume ±y×4, `2` pan ±y×16, `3` start up to y/16 |
| `Zxx` | Bit crush to xx bits (`01–0F`) |

Every track has a **second effect column** so a note can carry two effects; **FX2** on the bottom row shows or hides it (it starts hidden so the pattern text stays big). SAVE MOD keeps only one 0–F command per note and tells you when something was dropped.

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
| SEP | How far tracks lean to the Amiga sides (tracks 1+4 left, 2+3 right). Starts at 20%; 0% = everything centred, 100% = the full Amiga split. Double-click to reset. Track pans, instrument pans and `8xx` ignore it |

Samples are resampled on load so `C-3` plays them at their original pitch (16.5 kHz, the Amiga convention). For breaks, load the break, put `C-3` on line 0 and use `9xx` sample offsets to jump around it.

## In Ableton

* **HOST PLAY** — the song starts when Ableton starts and stops with it.
* **HOST BPM** — follows Ableton's tempo (at speed 6, one line = a 16th note). `Fxx` tempo changes in the song are ignored while this is on.
* The whole song, instruments and settings are saved inside your Live set.
* **RENDER WAV** writes the song once through as a 24-bit stereo WAV.

## Files

* `.prv` — Paula's Revenge song with samples (compressed). Format version 4 adds the second effect column, G–Z commands and sample rates (v3 added envelopes/filters/pans); older `.prv` files still load.
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
