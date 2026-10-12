# CHOMPI LOOP — phase 2.1: core note looper

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
| CHOMPI + Loop, tap | Undo the most recent recording pass (undoing the first take clears). |
| CHOMPI + Loop, hold 1 s | Clear the loop. |
| Play | Start / stop the loop and clock. Restarts a loop from the top. Sends MIDI Start/Stop in internal-clock mode (per the existing transport option). |
| Press + turn Tempo knob | Scroll the quantize grid through every value: 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, off. (Replaces TEMPO's clock division.) |
| CHOMPI + turn Tempo knob | Hop to the nearest straight / triplet grid just above or below the one you scrolled to (e.g. from 1/8T: up to 1/16, down to 1/8), and back. (Replaces TEMPO's arp randomness.) |
| Stepped pitch knob, in Chroma | Transposes the keys instead of re-pitching the sample (see below). In Slice it steps the pitch as in TEMPO. |

A "tap" is a press shorter than 0.4 s with no other key or knob touched; anything else
is a normal shift function.

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

**Chroma key transpose:** in Chroma, the *stepped* pitch knob (CHOMPI + Pitch by default;
the plain Pitch knob if `Pitch Quantize In Shift Menu` is `false`) moves the keys instead
of re-pitching the sample. Each key plays, records and sends over MIDI its own note plus
the transpose, so a transposed key sounds right *and* lands on the right MIDI note. The
steps are TEMPO's: a fifth, then a fourth (0, +7, +12, +19, +24 …; 0, −5, −12, −17, −24 …),
four detents per step, up to four octaves either way. The pitch knob's LED shows it: yellow
toward red going up, blue toward purple going down; pressing the knob in the shift menu
resets it to 0 along with the pitch. Only notes you play from then on are affected;
notes already in the loop keep their pitch. The key LEDs follow the pitches: a looped note
lights the key that would play that pitch *now*, so as you transpose down the pattern walks
up the keyboard and off the top, and the other way round. The continuous pitch knob still
re-pitches the sample (including reverse), MIDI-in notes are played as sent, and Slice is
unchanged. The transpose is saved with presets (as a 12th value in `presets.json`, which
stock TEMPO ignores) and with the A / B states.

**MIDI:** incoming notes are recorded, and live-quantized, too. Notes you play on the keys
are sent to MIDI out when they sound, and so is their loop playback; notes that came in
over MIDI are not echoed back. In external-clock mode, MIDI Start restarts the loop on
the first clock tick and MIDI Stop pauses it (when the transport-in option allows). The
first note's start snaps to the nearest beat of the incoming clock: played a little
early, it waits for that beat; a little late, it sounds right away and is recorded on
the beat.

**MIDI sync through:** in external-clock mode the incoming clock is passed on to MIDI
out when `MIDI Clock Out` is on, and incoming Start / Stop are passed on when the
transport option includes "out", so CHOMPI can receive sync and send it to the next
device at the same time. To send sync without the keys, set `MIDI Note Out` to `false`:
MIDI out then carries clock, Start / Stop and CCs, but no notes (`MIDI CC Out` still
controls the CCs).

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
   - Arm, play a simple pattern with repeated notes and gaps, close. Does it loop in time,
     with repeats and silences intact?
   - Close slightly early and slightly late. Does the length round to the right bar, and
     are notes near the bar line kept?
   - Overdub a second layer, then CHOMPI + Loop tap (undo), then hold (clear).
   - Switch between Chroma and Slice while overdubbing. Do both play back together?
   - Change the tempo while the loop plays. Notes should move closer/further apart
     but keep their length.
   - Live quantize (on by default, 1/16 grid): with a loop playing, tap keys just after a
     beat. Each hit should wait for the next 16th, and play back in the same place on the
     next pass. Switch to 1/4 and hold a key: nothing until the next beat, then it sounds
     for as long as you held it.
   - Hits you mean to land on a beat: do they sound on it, or do some jump a whole step
     later? (A key press takes a few ms to reach the looper, so a hit right on a line can
     count as just after it. If this happens a lot, a small allowance can fix it.)
   - Hold a key while the loop plays the same key. Your held note should keep sounding
     until you let go.
   - Tempo LEDs: with the loop playing, does the right LED pulse on the beat and the left
     one at the subdivision? If they're the other way round on your unit, say so (the
     mapping is a guess). Press + turn Tempo and watch keys 1–8 pulse at their own rates.
   - MIDI sync through (if you have two MIDI devices): clock CHOMPI from one and check the
     other, on CHOMPI's MIDI out, follows. Then set `MIDI Note Out` to `false` and check it
     still follows but no longer plays CHOMPI's notes.
   - Transpose (Chroma): record a short loop, then CHOMPI + turn the pitch knob. The loop
     should keep its pitch while new keys play transposed, and the lit keys should shift
     along the keyboard (off the end after two octaves or so). Save a preset, change the
     transpose, recall the preset: it should come back. Press the pitch knob in the shift
     menu: back to 0.
   - Set `Loop Live Quantize` to `false`: notes sound the moment you play them and are
     pulled to the grid on the next pass.
   - MIDI sync (if you have a clock source): start/stop from the external device.
   - Shift functions (preset save/copy/erase, shift + knobs) still behave as in TEMPO.

## Known limitations in this build

- The shift menu's LEDs flash briefly on a CHOMPI tap (cosmetic).
- The Loop key alone does nothing yet (phase 3), and the arpeggiator can't be turned on.
  TEMPO's arp rest patterns (shift + Loop), arp randomness (shift + Tempo), and clock
  division (press + turn Tempo) are gone.
- Bars are 4/4. Loops are capped at 60 bars and 1,024 notes.
- Repeating the same key quickly retriggers its voice (cutting the previous tail), and
  each engine has 8 voices. A stacking option is planned.
- In internal-clock mode the clock realigns to your first note to within one clock
  tick (1/24 beat), which affects the delay's sync and MIDI clock out by at most that.
- Which of the two LEDs above the Tempo knob is "left" is a guess; swap them if needed.
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
- Quantize strength on the hardware, same-note voice stacking, saving loops to
  `/Loops` on the SD card, per-engine loop lengths.

## Developer notes

- `LoopCore.h`: all looper logic, plain C++ with no libDaisy dependencies.
  Timing is in units of 1/768 beat (the 24 PPQN clock × 32). It also sounds the notes
  you play ("live notes"), which is what lets live quantize hold a note back until its
  grid line.
- `NoteLooper.h`: connects LoopCore to the engines, MIDI, clock, options, and the
  CHOMPI / Loop key gestures (fed from `ui.h`). Looped notes go to the engines as
  `Source::SEQUENCER`, live notes as `Source::USER` (like a key press), so the engines
  keep a voice going while either wants it. Holds the Chroma transpose and picks each
  Chroma note's engine voice from its pitch (the key that plays it at the current
  transpose), which is what moves the key LEDs.
- `StateSaver.h` / `PresetManager.h`: stock files with one addition each, the transpose
  (an A / B state field; preset value 11). The rest is unchanged.
- `NormalPage.h` / `MidiManager.h`: plain notes (no arpeggiator) are handed to the
  looper to sound instead of being sent to the engines directly; the arpeggiator paths
  are unchanged and only record into the looper.
- `OptionsManager.h`: the three looper options and `MIDI Note Out`, plus a guard so an
  options.json with fewer entries (like stock TEMPO's) can't crash the parser.
- `MidiManager.h`: besides routing notes to the looper, passes clock / Start / Stop
  through in external-clock mode and drops note messages when `MIDI Note Out` is off.
- `clockManager.h`: adds one shared timeline that advances on the internal timer or
  incoming MIDI clock, interpolated between ticks.
- `tests/test_loopcore.cpp`: tests for LoopCore. The cloud build runs them before every
  firmware build, so a logic regression stops the build with a clear failure.
