# CHOMPI LOOP — phase 2.2: note looper with a step view

A standalone firmware based on TEMPO. TEMPO's latching arpeggiator is replaced by a
polyphonic, quantized note-event looper that follows the internal clock or MIDI clock.
The loop records *notes* (which key, when, how long it was held), not audio, so
samples are retriggered fresh on every pass and tempo changes never alter pitch or
cut notes short. A step view lets you see and program the loop on the keys.

`chompi-loop` is a placeholder name. Rename the folder and `.bin` to whatever you like.

## Getting the .bin

The firmware is built in the cloud by GitHub Actions in your fork
(see `START-HERE.md` in the download). The workflow takes stock TEMPO from
`firmware/chompi-tempo`, drops these files from `mods/chompi-loop/src` on top,
builds with GCC 13.3, and offers `CHOMPI_LOOP.bin` for download.

To change the mod, edit or replace files in `mods/chompi-loop/src` and commit;
a new build starts automatically.

## SD card

Use your existing TEMPO-style card. Remove the current `.bin` (keep a copy on your
computer) and add `CHOMPI_LOOP.bin`; only one `.bin` can be on the card. The
`chromatic`, `slice`, and `buffer` folders, `presets.json`, and `options.json` stay
as they are. Other folders (like `FRIZZ`) are ignored. To go back, swap the `.bin`.

On first boot the firmware rewrites `options.json` with four new entries
(see below). Stock TEMPO ignores them, so the card still works with TEMPO.

## Controls in this build

Mode switch **down** (the CHOMPI key is shift):

| Gesture | Action |
|---|---|
| CHOMPI tap, no loop | Arm. Loop LED blinks red. Keys still play normally. |
| CHOMPI tap, armed | Disarm. |
| First note while armed | Loop starts; that note is beat 1. Starts the transport if it was stopped. |
| CHOMPI tap while recording | Close, rounded to the nearest bar (measured from when you pressed). Notes played before the bar line are still recorded. |
| CHOMPI tap while playing | Open for overdubbing (Loop LED pink). Tap again to close. Length never changes. |
| CHOMPI + Loop, tap | Undo the most recent recording pass or step edit (undoing the first take clears). |
| CHOMPI + Loop, hold 1 s | Clear the loop. |
| Play (tap) | Start / stop the loop and clock, when you let go of the key. Restarts a loop from the top. Sends MIDI Start/Stop in internal-clock mode (per the existing transport option). |
| Play + Loop, hold 1 s | Switch between the key view and the **step view** (see below). Either order. Keys held when the second is up pick the note(s) the step view places. |
| Press + turn Tempo knob | Scroll the quantize grid through every value: 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, off. (Replaces TEMPO's clock division.) |
| CHOMPI + turn Tempo knob | Hop to the nearest straight / triplet grid just above or below the one you scrolled to (e.g. from 1/8T: up to 1/16, down to 1/8), and back. (Replaces TEMPO's arp randomness.) |
| Stepped pitch knob, in Chroma | Transposes everything Chroma plays — the keys and the loop — in fifths and fourths (see below). In Slice it steps the pitch as in TEMPO. |

A "tap" is a press shorter than 0.4 s with no other key or knob touched; anything else
is a normal shift function. Play held together with Loop never toggles the transport.

**Quantize grid:** a grid change only affects notes you play afterwards; notes already
in the loop stay where they are. For 1.5 s after a change, white keys 1–9 show the grid:
keys 1–8 are 1/4 through 1/32T (straight = teal, triplet = pink), key 9 is off (white).
Bright = the grid new notes will use; medium = the grid you scrolled to, while you've
hopped away from it with CHOMPI + Tempo; dim = the others. While the transport runs, each
key pulses at its own subdivision's rate, so you can see how fast each one is. Turning
the Tempo knob while pressed doesn't count toward tap tempo. Mode switch **up**: the
CHOMPI key records audio samples exactly as in TEMPO.

**Tempo LEDs:** while the transport runs, the right LED above the Tempo knob pulses on
every beat and the left one pulses at the current quantize subdivision (steady and dim
when the grid is off). The pulse follows the loop's beats when there is a loop, and the
clock from when you pressed Play (or the MIDI song position) when there isn't. Stopped,
they show the tempo as in TEMPO.

**Live quantize** (on by default): while the loop is running — recording, overdubbing,
or just playing along — a note you play waits for the next line of the current grid
before it sounds, like TEMPO's play mode. A hit even slightly after a line waits for the
following one. When you're recording, the note is recorded where it sounded, so what you
hear is what loops. Notes sound right away when a hit lands exactly on a line, when the
transport is stopped, when there's no loop yet (the first note of a new loop is beat 1),
and when the grid is off.

**Loop LED:** blinking red = armed, red = recording, pink = overdub, green = playing
(dim when stopped), blinking yellow = note memory full.
**Play LED:** teal while the transport runs; white in the step view (dim white when
stopped in the step view).

**Chroma transpose:** in Chroma, the *stepped* pitch knob (CHOMPI + Pitch by default;
the plain Pitch knob if `Pitch Quantize In Shift Menu` is `false`) transposes everything
the looper plays — the keys you press and the notes in the loop — instead of re-pitching
the sample. The steps are TEMPO's: a fifth, then a fourth (0, +7, +12, +19, +24 …;
0, −5, −12, −17, −24 …), four detents per step, up to four octaves either way. Notes are
stored untransposed and shifted as they sound, so the loop moves with the knob and a
looped note still lights the key you played it on. MIDI out follows: a transposed key
sends its note plus the transpose, and so does the loop. A change takes effect from the
next note that starts; a note already sounding keeps its pitch (stock TEMPO re-pitches
sounding notes at once — that's the one difference). The pitch knob's LED shows the
transpose: yellow toward red going up, blue toward purple going down; pressing the knob
in the shift menu resets it to 0 along with the pitch. The continuous pitch knob still
re-pitches the sample (including reverse), MIDI-in notes are played as sent, and Slice is
unchanged. The transpose is saved with presets (as a 12th value in `presets.json`, which
stock TEMPO ignores) and with the A / B states.

### Step view

Hold Play + Loop for a second (in either order) to switch the keys from playing notes to
showing and editing the loop; the same hold switches back. The Play LED turns white.

- **What you see.** One key per step of the current quantize grid (1/16 when the grid is
  off), left to right across the keyboard, black keys included. A key is lit white when
  a note of the current engine (Chroma or Slice — flip the engine and the view flips)
  starts in that step, red where the playhead is, yellow for both. Everything is either
  full on or off. A finer grid shows finer detail and the playhead runs faster; a coarser
  grid lumps more of the loop into each key, lit if anything starts inside it.
- **Pages.** The keys show whole bars: 6 bars at 1/4, 3 at 1/8, 2 at 1/4T, 1 at 1/16 or
  1/8T (16 or 24 keys), half a bar at 1/32 or 1/16T, a quarter at 1/32T. The page follows
  the playhead. Keys past the page, and keys past the loop's end, are dark and do nothing.
- **Programming.** Press a dark key to place a note at the start of that step, one step
  long. It plays when the loop gets there. Press a lit key to erase every note of the
  current engine that starts in that step. Each press is its own undo layer, so CHOMPI +
  Loop tap takes back the last one (erased notes come back). Edits work whether or not
  overdub is on, and with the transport stopped.
- **Which note.** The keys you're holding when the Play + Loop hold completes are what
  step presses place — one key for a single note, several for a chord. Keys pressed
  after Play and Loop are both down don't sound; they only select. A key you were already
  playing when you started the hold keeps sounding (and is recorded if you're
  overdubbing) until you let go. With no key held, every press places a random key's
  note (Chroma: a random pitch across the 25 keys; Slice: a random slice). Re-enter the
  step view to change the selection.
- **No loop yet.** The step view shows one empty bar (with the playhead running through
  it if the transport is on). The first note you place creates a 1-bar loop and starts
  it: aligned to the last bar line if the transport was running, or starting that moment
  if it wasn't.
- MIDI-in notes still play and record normally in the step view. The shift menu (CHOMPI
  held) works as usual on top of it.

**MIDI:** incoming notes are recorded, and live-quantized, too. Notes you play on the keys
are sent to MIDI out when they sound, and so is their loop playback; notes that came in
over MIDI are not echoed back. In external-clock mode, MIDI Start restarts the loop on
the first clock tick and MIDI Stop pauses it (when the transport-in option allows). The
first note's start snaps to the nearest beat of the incoming clock: played a little
early, it waits for that beat; a little late, it sounds right away and is recorded on
the beat.

**MIDI sync through** (not working on hardware yet — see limitations): in external-clock
mode the incoming clock is meant to be passed on to MIDI out when `MIDI Clock Out` is on,
and incoming Start / Stop when the transport option includes "out". To send sync without
the keys, set `MIDI Note Out` to `false`: MIDI out then carries clock, Start / Stop and
CCs, but no notes (`MIDI CC Out` still controls the CCs).

### options.json

| Name | Values | Default |
|---|---|---|
| `Loop Quantize Grid` | grid at power-on, in notes per whole note: 4, 6 (1/4T), 8, 12 (1/8T), 16, 24 (1/16T), 32, 48 (1/32T), or 0 = off | 16 |
| `Loop Quantize Strength` | 0–100 (%): how far each note start is pulled toward the grid | 100 |
| `Loop Live Quantize` | `true`: while the loop runs, notes you play wait for the next grid line before they sound, and are recorded there. `false`: notes sound the moment you play them; your timing is recorded and pulled to the nearest grid line on playback | `true` |
| `MIDI Note Out` | `false`: no note on / off messages on MIDI out (clock, Start / Stop and CCs are unaffected) | `true` |

Note *lengths* are always kept exactly as you held them. With live quantize, a note's
length counts from when it actually sounds, so if you let go before its grid line it
still plays, for as long as you held the key. With quantize strength below 100 %, a
live note is moved only part of the way toward the next line (never earlier than you
played it). In both modes each note keeps the grid that was active when you played it.

If you stop the transport while a note is waiting for its line, it sounds right away if
you're still holding the key, and is dropped if you've already let go.

## Testing checklist

Please send back:

1. If the build fails: the error lines from the "Build the firmware" step of the Actions log.
   If it succeeds: the memory table and size printed at the end of that step.
2. Results of these:
   - Play: a tap still starts / stops the transport (now when you let go). Holding Play
     on its own does nothing until you release it.
   - Transpose (Chroma): record a short loop, then CHOMPI + turn the pitch knob. The loop
     and new keys should move together, by fifths and fourths; a note already sounding
     finishes at its old pitch. The lit keys stay where you played them. Save a preset,
     change the transpose, recall the preset: it should come back. Press the pitch knob
     in the shift menu: back to 0. Check MIDI out follows the transpose, for keys and loop.
   - Step view, with a 1/16 loop playing: hold Play + Loop for a second. 16 keys; white
     where your notes start, red running across at 16th-note speed. Change the grid
     (press + turn Tempo): 1/4 shows 6 bars on 24 keys (lumped), 1/32 shows half a bar at
     a time and the keys page as the playhead crosses the middle of the bar.
   - Step view, placing: hold a key, then Play + Loop. Press dark keys: that note lands on
     those steps, one 16th long, and plays when the loop reaches them. Press a lit key:
     its notes go silent. CHOMPI + Loop tap: the last press is undone (erased notes come
     back). Enter the step view with two keys held: each press places both. Enter it with
     nothing held: each press places a random note.
   - Step view with no loop (clear first): hold Play + Loop, press a key. A 1-bar loop
     starts playing with that note in it. Try it with the transport running and stopped.
   - Switch engines while in the step view: the display follows. Slice notes placed from
     the step view play the slice of the selected key.
   - Keys past the loop end (e.g. a 1-bar loop at 1/4 grid: keys 5–24) stay dark and do
     nothing.
   - Hold a key, then Play + Loop: the held note keeps sounding until you let go, even
     after the view switches. Keys pressed while Play + Loop are down stay silent.
   - Earlier checks that should still pass: live quantize waits for the next line; a held
     key isn't cut by the loop playing the same key; tempo LEDs; grid view pulses;
     shift functions (preset save / copy / erase, shift + knobs) as in TEMPO.

## Known limitations in this build

- MIDI sync through did not work in hardware testing (clocked from one device, the next
  device on CHOMPI's MIDI out didn't follow). Parked; possibly a hardware limitation.
- Transposing while a note sounds doesn't re-pitch it; the new transpose applies from
  the next note that starts.
- Step edits made while overdubbing share an undo layer with the notes you overdub
  after them, until the next edit.
- The step view's placed notes are one step long at the tempo when you placed them
  (like played notes, lengths are in time, not beats).
- The shift menu's LEDs flash briefly on a CHOMPI tap (cosmetic).
- The Loop key alone does nothing yet (phase 3), and the arpeggiator can't be turned on.
  TEMPO's arp rest patterns (shift + Loop), arp randomness (shift + Tempo), and clock
  division (press + turn Tempo) are gone.
- Bars are 4/4. Loops are capped at 60 bars and 1,024 notes (erased notes count until
  the loop is cleared).
- Repeating the same key quickly retriggers its voice (cutting the previous tail), and
  each engine has 8 voices. A stacking option is planned.
- In internal-clock mode the clock realigns to your first note to within one clock
  tick (1/24 beat), which affects the delay's sync and MIDI clock out by at most that.
- Transpose is a single setting for Chroma (not per A / B state while you're in Slice —
  switching A / B in Slice applies Chroma's saved transpose for that state too, as TEMPO
  does for Chroma's other settings).
- `Loop Live Quantize` and `MIDI Note Out` are set in `options.json` only (no gesture).

## Roadmap (agreed design)

**Phase 3 — Loop key lengthen / shorten**
- Loop tap: open for lengthening and overdubbing without stopping. The loop stops
  wrapping and plays on past the old end into empty bars. Loop tap again sets the new
  length (rounded to the current edit unit). Closing before the old end shortens the
  loop; notes past the end are kept, silent, and return if you lengthen again.
- (The sustain toggle on a lone Loop hold is dropped: held keys and the envelope
  already cover it. "Loop hold alone" is free for something else.)

**Phase 4 — Multiply / divide**
- Hold Loop + turn Encoder 1: a live, audible ladder `… ÷3, ÷2, ×1, ×2, ×3, ×4`, down to
  ÷64. Divisions play a fraction of the full loop without erasing anything; turning
  right revives the full loop before any multiplying starts. Multiplying copies the
  notes into the new bars. The full loop is set by the first close or the last paste
  (and, assumed, a Loop-key lengthen).
- (The per-bar overview on the white keys is replaced by the step view, done in 2.2.)

**Phase 5 — Copy / paste**
- Hold Loop + press Encoder 1 to enter. Turn to move a cursor, press to mark, turn to
  extend (up to 6 units), press to copy, turn to choose a destination (any unit start,
  including just after the last bar), press to paste (repeatable). Paste replaces the
  destination; pasting past the end extends the loop.
- Shift + turn Encoder 1 changes the edit unit (granularity): 4/4 bar, 2 beats, 1 beat,
  1/2, 1/4, 1/8, 1/16, 1/32 beat. The step view and selections follow the unit.

**Phase 6 — Polish**
- Quantize strength on the hardware, same-note voice stacking, saving loops to
  `/Loops` on the SD card, per-engine loop lengths.

## Developer notes

- `LoopCore.h`: all looper logic, plain C++ with no libDaisy dependencies.
  Timing is in units of 1/768 beat (the 24 PPQN clock × 32). It also sounds the notes
  you play ("live notes"), which is what lets live quantize hold a note back until its
  grid line. Step edits: `EnsureLoop`, `BeginEditPass`, `HasNotesIn`, `EraseRange`,
  `InsertNote`, `StepMask`. Erased notes stay in the array with `kFlagErased` (their
  old pass parked in `rec_odo`) so `Undo` can restore them.
- `NoteLooper.h`: connects LoopCore to the engines, MIDI, clock, options, and the
  CHOMPI / Loop / Play key gestures (fed from `ui.h`). Looped notes go to the engines as
  `Source::SEQUENCER`, live notes as `Source::USER` (like a key press), so the engines
  keep a voice going while either wants it. Applies the Chroma transpose as notes
  sound (`NoteOut`), remembering the MIDI note sent per voice for the note-off. Runs the
  step view: the Play + Loop hold, the page / mask / playhead the UI draws
  (`UpdateStepView`, every 8th block), and the edits (`StepEdit`).
- `NormalPage.h`: plain notes (no arpeggiator) are handed to the looper to sound
  instead of being sent to the engines directly, untransposed; the arpeggiator paths
  are unchanged and only record into the looper. Draws the step view
  (`DrawLooperSteps`) and turns step-view key presses into edits (`StepPress`).
- `ui.h`: feeds the Play key to the looper and lets it swallow key presses while
  Play + Loop are held (`OnKeyEdge`).
- `MenuPage.h`, `StateSaver.h`, `PresetManager.h`: the transpose on the stepped knob,
  its LED, knob-press reset, and saving (A / B state field; preset value 11).
- `OptionsManager.h`: the three looper options and `MIDI Note Out`, plus a guard so an
  options.json with fewer entries (like stock TEMPO's) can't crash the parser.
- `MidiManager.h`: besides routing notes to the looper, passes clock / Start / Stop
  through in external-clock mode and drops note messages when `MIDI Note Out` is off.
- `clockManager.h`: adds one shared timeline that advances on the internal timer or
  incoming MIDI clock, interpolated between ticks.
- `tests/test_loopcore.cpp`: 31 tests for LoopCore. The cloud build runs them before
  every firmware build, so a logic regression stops the build with a clear failure.
