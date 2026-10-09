# CHOMPI LOOP — phase 2: core note looper

A standalone firmware based on TEMPO. TEMPO's latching arpeggiator is replaced by a
polyphonic, quantized note-event looper that follows the internal clock or MIDI clock.
The loop records *notes* (which key, when, how long it was held), not audio, so
samples are retriggered fresh on every pass and tempo changes never alter pitch or
cut notes short.

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

On first boot the firmware rewrites `options.json` with two new entries
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
| CHOMPI + Loop, tap | Undo the most recent recording pass (undoing the first take clears). |
| CHOMPI + Loop, hold 1 s | Clear the loop. |
| Play | Start / stop the loop and clock. Restarts a loop from the top. Sends MIDI Start/Stop in internal-clock mode (per the existing transport option). |

A "tap" is a press shorter than 0.4 s with no other key or knob touched; anything else
is a normal shift function. Mode switch **up**: the CHOMPI key records audio samples
exactly as in TEMPO.

**Loop LED:** blinking red = armed, red = recording, pink = overdub, green = playing
(dim when stopped), blinking yellow = note memory full.

**MIDI:** incoming notes are recorded too. Looped notes from the keyboard are sent to
MIDI out; looped notes that came in over MIDI are not echoed back. In external-clock
mode, MIDI Start restarts the loop on the first clock tick and MIDI Stop pauses it
(when the transport-in option allows). The first note's start snaps to the nearest beat
of the incoming clock.

### options.json

| Name | Values | Default |
|---|---|---|
| `Loop Quantize Grid` | notes per whole note: 4, 8, 12 (8th triplets), 16, 24 (16th triplets), 32, or 0 = off | 16 |
| `Loop Quantize Strength` | 0–100 (%): how far each note is pulled toward the grid | 100 |

Quantizing happens on playback; your original timing is always kept, so changing these
later re-quantizes existing loops.

## Testing checklist

Please send back:

1. If the build fails: the error lines from the "Build the firmware" step of the Actions log.
   If it succeeds: the memory table and size printed at the end of that step.
2. Results of these:
   - Arm, play a simple pattern with repeated notes and gaps, close. Does it loop in time,
     with repeats and silences intact?
   - Close slightly early and slightly late. Does the length round to the right bar, and
     are notes near the bar line kept?
   - Overdub a second layer, then CHOMPI + Loop tap (undo), then hold (clear).
   - Switch between Chroma and Slice while overdubbing. Do both play back together?
   - Change the tempo while the loop plays. Notes should move closer/further apart
     but keep their length.
   - MIDI sync (if you have a clock source): start/stop from the external device.
   - Shift functions (preset save/copy/erase, shift + knobs) still behave as in TEMPO.

## Known limitations in this build

- The shift menu's LEDs flash briefly on a CHOMPI tap (cosmetic).
- The Loop key alone does nothing yet (phase 3), and the arpeggiator can't be turned on.
  TEMPO's arp rest patterns (shift + Loop) are gone.
- Bars are 4/4. Loops are capped at 60 bars and 1,024 notes.
- Repeating the same key quickly retriggers its voice (cutting the previous tail), and
  each engine has 8 voices. A stacking option is planned.
- In internal-clock mode the clock realigns to your first note to within one clock
  tick (1/24 beat), which affects the delay's sync and MIDI clock out by at most that.

## Roadmap (agreed design)

**Phase 3 — Loop key lengthen / shorten**
- Loop tap: open for lengthening and overdubbing without stopping. The loop stops
  wrapping and plays on past the old end into empty bars. Loop tap again sets the new
  length (rounded to the current edit unit). Closing before the old end shortens the
  loop; notes past the end are kept, silent, and return if you lengthen again.
- Loop hold alone (1 s, nothing else touched): key sustain toggle. Using Encoder 1
  during the hold cancels it.

**Phase 4 — Bar view and multiply / divide**
- Hold Loop + turn Encoder 1: a live, audible ladder `… ÷3, ÷2, ×1, ×2, ×3, ×4`, down to
  ÷64. Divisions play a fraction of the full loop without erasing anything; turning
  right revives the full loop before any multiplying starts. Multiplying copies the
  notes into the new bars. The full loop is set by the first close or the last paste
  (and, assumed, a Loop-key lengthen).
- Bar view on white keys 1–15 (one bar per key, pages beyond 15). Each bar's hue steps
  a little further around the color wheel (~12°, a full cycle every 30 bars). Selected
  bars flash; the playing bar glows brighter; bars past the end are dark.

**Phase 5 — Copy / paste**
- Hold Loop + press Encoder 1 to enter. Turn to move a cursor, press to mark, turn to
  extend (up to 6 units), press to copy, turn to choose a destination (any unit start,
  including just after the last bar), press to paste (repeatable). Paste replaces the
  destination; pasting past the end extends the loop.
- Shift + turn Encoder 1 changes the edit unit (granularity): 4/4 bar, 2 beats, 1 beat,
  1/2, 1/4, 1/8, 1/16, 1/32 beat. The bar view and selections follow the unit.

**Phase 6 — Polish**
- Quantize grid / strength on the hardware, same-note voice stacking, saving loops to
  `/Loops` on the SD card, per-engine loop lengths.

## Developer notes

- `LoopCore.h`: all looper logic, plain C++ with no libDaisy dependencies.
  Timing is in units of 1/768 beat (the 24 PPQN clock × 32).
- `NoteLooper.h`: connects LoopCore to the engines, MIDI, clock, options, and the
  CHOMPI / Loop key gestures (fed from `ui.h`).
- `clockManager.h`: adds one shared timeline that advances on the internal timer or
  incoming MIDI clock, interpolated between ticks.
- `OptionsManager.h`: the two looper options, plus a guard so an options.json with
  fewer entries (like stock TEMPO's) can't crash the parser.
- `tests/test_loopcore.cpp`: tests for LoopCore. The cloud build runs them before every
  firmware build, so a logic regression stops the build with a clear failure.
