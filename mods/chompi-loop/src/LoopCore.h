/** LoopCore
 *  Hardware-independent logic for the polyphonic note-event looper.
 *
 *  Everything here is plain C++ with no libDaisy dependencies, so it can be
 *  compiled and tested on a computer. NoteLooper.h connects it to CHOMPI.
 *
 *  Positions are measured in "units": the 24 PPQN clock subdivided by 32,
 *  so one beat = 768 units. 768 divides evenly by every quantize grid we use
 *  (including triplets), which keeps all the timing math in integers.
 *
 *  LoopCore also sounds the notes you play by hand ("live notes"). That's what lets
 *  live quantize hold a note back until the next grid line, so what you hear while
 *  playing is what gets recorded.
 *
 *  The step view edits the loop directly: InsertNote / EraseRange / HasNotesIn /
 *  StepMask, each edit as its own undo layer (BeginEditPass).
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace looper
{

constexpr int32_t  kUnitsPerTick   = 32;
constexpr int32_t  kUnitsPerBeat   = 24 * kUnitsPerTick; // 768
constexpr int32_t  kBeatsPerBar    = 4;                  // 4/4 for now
constexpr int32_t  kUnitsPerBar    = kUnitsPerBeat * kBeatsPerBar;
constexpr int32_t  kMaxBars        = 60;
constexpr int32_t  kMaxLength      = kMaxBars * kUnitsPerBar;
constexpr size_t   kMaxEvents      = 1024;
constexpr size_t   kMaxPendingOffs = 32;
constexpr size_t   kMaxLiveNotes   = 24;   // notes played by hand, sounding or about to
constexpr uint32_t kMinNoteUs      = 1000; // shortest note we'll schedule (1 ms)

/** Quantize grids, coarse to fine, alternating straight and triplet, then off.
 *  Index:  0    1     2    3     4     5      6     7      8
 *  Grid:   1/4  1/4T  1/8  1/8T  1/16  1/16T  1/32  1/32T  off  */
constexpr int     kNumGrids              = 9;
constexpr int     kGridOff               = 8;
constexpr int     kGridSixteenth         = 4;
constexpr int32_t kGridUnits[kNumGrids]  = {768, 512, 384, 256, 192, 128, 96, 64, 0};
constexpr uint8_t kGridNotes[kNumGrids]  = {4, 6, 8, 12, 16, 24, 32, 48, 0}; // notes per whole note

enum class State : uint8_t
{
    EMPTY,     // no loop
    ARMED,     // waiting for the first note
    RECORDING, // first pass: length not set yet (or set, waiting for the bar line)
    PLAYING,   // loop closed and playing (or paused, see IsRunning)
    OVERDUB,   // loop playing and recording new notes on top
};

constexpr uint8_t kFlagHeld     = 1 << 0; // key still held: length unknown, don't play yet
constexpr uint8_t kFlagFromMidi = 1 << 1; // arrived over MIDI: don't echo to MIDI out
constexpr uint8_t kFlagSnapped  = 1 << 2; // quantized when recorded (live): play pos as-is
constexpr uint8_t kFlagErased   = 1 << 3; // erased from the step view: silent until undone

struct Event
{
    int32_t  pos;     // raw (unquantized) position from loop start, in units
    uint32_t rec_odo; // playhead odometer when recorded (prevents instant double hits).
                      // While kFlagErased is set: the pass the note belonged to.
    uint32_t dur_us;  // note length in µs. While kFlagHeld is set: the note-on time.
    int8_t   key;     // engine voice id (Hardware::SwId value)
    int8_t   nn;      // transpose in semitones
    uint8_t  engine;  // 0 = chroma, 1 = slice
    uint8_t  note;    // MIDI note number, for MIDI out
    uint16_t pass;    // recording pass number, for undo (an erased note: the erasing pass)
    uint8_t  flags;
    uint8_t  grid;    // quantize grid (index into kGridUnits) when this note was played
};

/** Where the looper sends its notes. NoteLooper implements this. */
class Output
{
  public:
    virtual ~Output() {}

    /** A looped note starts / stops. */
    virtual void LoopNoteOn(const Event &ev) = 0;
    virtual void
    LoopNoteOff(uint8_t engine, int8_t key, uint8_t note, bool from_midi)
        = 0;

    /** A note played by hand starts / stops. Kept apart from looped notes: the
     *  engines keep a voice going while either the hand or the loop wants it, so a
     *  looped note ending never cuts off a key you're still holding. */
    virtual void LiveNoteOn(const Event &ev)  = 0;
    virtual void LiveNoteOff(const Event &ev) = 0;
};

class LoopCore
{
  public:
    void Init(Output *out)
    {
        out_         = out;
        record_grid_ = kGridSixteenth;
        strength_    = 100;
        live_        = true;
        running_  = false;
        have_time_ = false;
        sync_      = false;
        beat_phase_ = 0;
        odo_        = 0;
        now_us_     = 0;
        request_clock_reset_ = false;
        for(auto &p : pending_)
            p.active = false;
        for(auto &n : live_notes_)
            n.active = false;
        live_seq_ = 0;
        ClearAll();
    }

    /** Grid for notes played from now on (index into kGridUnits).
     *  Notes already in the loop keep the grid they were played with. */
    void SetRecordGrid(int grid_index)
    {
        record_grid_ = grid_index < 0 ? 0 : (grid_index >= kNumGrids ? kGridOff : grid_index);
    }
    int GetRecordGrid() const { return record_grid_; }

    /** 0-100 (%): how far a note is pulled toward its grid line */
    void SetStrength(int32_t strength_pct)
    {
        strength_ = strength_pct < 0 ? 0 : (strength_pct > 100 ? 100 : strength_pct);
    }

    /** true (default): while the loop runs, a note you play waits for the next
     *  grid line before it sounds, and is recorded on that line.
     *  false: notes sound the moment you play them; your timing is recorded as
     *  played and pulled to the grid on playback. */
    void SetLiveQuantize(bool live) { live_ = live; }

    /** sync: following an external clock. beat_phase: units since the last beat line. */
    void SetSyncInfo(bool sync, int32_t beat_phase)
    {
        sync_       = sync;
        beat_phase_ = beat_phase;
    }

    /** Call once per audio block with the shared timeline position and the time in µs. */
    void Process(uint32_t timeline, uint32_t now_us)
    {
        now_us_       = now_us;
        int32_t delta = 0;
        if(have_time_)
        {
            const int32_t d = static_cast<int32_t>(timeline - last_timeline_);
            if(d > 0)
            {
                delta          = d > kUnitsPerBar ? kUnitsPerBar : d; // ignore glitches
                last_timeline_ = timeline;
            }
        }
        else
        {
            last_timeline_ = timeline;
            have_time_     = true;
        }

        if(running_ && delta > 0)
            Advance(delta);

        ReleaseDueLiveNotes();
        ServiceNoteOffs();
    }

    // ---------------- performance input ----------------

    /** A note played by hand (keyboard or MIDI in). It's recorded while recording
     *  or overdubbing.
     *  sound = true: LoopCore also sounds it, and stops it after NoteOff. With live
     *  quantize on and the loop running, it waits for the next grid line (a note
     *  even slightly after a line waits for the following one) and is recorded
     *  there; otherwise it sounds right away.
     *  sound = false: something else is already sounding it (e.g. the arpeggiator). */
    void NoteOn(uint8_t engine,
                int8_t  key,
                int8_t  nn,
                uint8_t note,
                bool    from_midi,
                bool    sound = false)
    {
        const bool first = state_ == State::ARMED;
        if(first)
            BeginFirstPass();

        // Where the note belongs. With live quantize: the next grid line. The first
        // note of a loop is beat 1, so it belongs at the loop start instead (with
        // MIDI clock that's the nearest beat, which may be just behind it).
        const int32_t g      = kGridUnits[record_grid_];
        int32_t       target = pos_;
        if(live_)
            target = first ? PullTo(pos_, 0, g) : PullForward(pos_, g);

        // While the loop runs, the note waits for the playhead to get there.
        const bool grid_running
            = running_
              && (state_ == State::RECORDING || state_ == State::PLAYING
                  || state_ == State::OVERDUB);
        const int32_t wait = grid_running && target > pos_ ? target - pos_ : 0;

        if(sound)
            AddLiveNote(engine, key, nn, note, from_midi, wait);

        const bool recording
            = state_ == State::RECORDING || (state_ == State::OVERDUB && running_);
        if(!recording)
            return;

        if(num_events_ >= kMaxEvents)
        {
            full_ = true;
            return;
        }

        int32_t p     = target;
        uint8_t flags = kFlagHeld | (from_midi ? kFlagFromMidi : 0);
        if(live_)
        {
            // Stored where it sounds. (Its length stays exactly as played.)
            if(state_ == State::OVERDUB)
                p = Mod(p, len_); // a grid line at the loop end is the downbeat
            flags |= kFlagSnapped;
        }

        Event &e  = events_[num_events_++];
        e.pos     = p;
        e.rec_odo = odo_;
        e.dur_us  = now_us_; // note-on time until the key is released
        e.key     = key;
        e.nn      = nn;
        e.engine  = engine;
        e.note    = note;
        e.pass    = pass_;
        e.flags   = flags;
        e.grid    = static_cast<uint8_t>(record_grid_);
    }

    /** Key released. Returns true if LoopCore is sounding this note (it stops it
     *  itself, as long after it started as the key was held); false if the
     *  caller should stop it. */
    bool NoteOff(int8_t key)
    {
        // match the most recent held event for this key
        for(size_t i = num_events_; i-- > 0;)
        {
            Event &e = events_[i];
            if((e.flags & kFlagHeld) && e.key == key)
            {
                uint32_t dur = now_us_ - e.dur_us;
                e.dur_us     = dur < kMinNoteUs ? kMinNoteUs : dur;
                e.flags &= static_cast<uint8_t>(~kFlagHeld);
                break;
            }
        }

        // the live note on this key (there's at most one still held)
        for(auto &n : live_notes_)
        {
            if(!n.active || n.released || n.ev.key != key)
                continue;
            const uint32_t held = now_us_ - n.press_us;
            n.held_us           = held < kMinNoteUs ? kMinNoteUs : held;
            n.released          = true;
            if(n.sounding)
            {
                n.off_us = n.start_us + n.held_us;
                ReleaseDueLiveNotes(); // (right now, if it sounded from the press)
            }
            // Still waiting for its grid line: it plays when it gets there.
            return true;
        }
        return false;
    }

    // ---------------- controls ----------------

    /** CHOMPI tap. press_timeline: the shared timeline position when the key went down. */
    void ChompiTap(uint32_t press_timeline)
    {
        switch(state_)
        {
            case State::EMPTY: state_ = State::ARMED; break;
            case State::ARMED: state_ = State::EMPTY; break;
            case State::RECORDING:
                if(len_ == 0)
                    CloseFirstPass(press_timeline);
                else
                    overdub_after_close_ = !overdub_after_close_; // tapped again before the bar line
                break;
            case State::PLAYING:
                state_ = State::OVERDUB;
                pass_++;
                break;
            case State::OVERDUB: state_ = State::PLAYING; break;
        }
    }

    /** Transport start. Restarts a closed loop from the top.
     *  start_pos <= 0: a negative value delays the downbeat by that many units
     *  (used to land it on the next MIDI clock tick after a MIDI Start). */
    void Start(int32_t start_pos = 0)
    {
        running_ = true;
        ResolveWaitingLiveNotes(); // the grid starts over
        if(state_ == State::PLAYING || state_ == State::OVERDUB)
        {
            if(start_pos > 0)
                start_pos = 0;
            pos_       = start_pos;
            scan_from_ = start_pos - 1; // include notes sitting exactly on the downbeat
        }
    }

    /** Transport stop. Pauses everything and releases looper notes. Live notes
     *  you're holding keep sounding until you let go. */
    void Stop()
    {
        running_ = false;
        AllNotesOff();
        ResolveWaitingLiveNotes();
    }

    /** Remove the most recent recording pass (or step edit). Notes that pass erased
     *  come back. Undoing the first take clears the loop. */
    void Undo()
    {
        if(state_ == State::ARMED || state_ == State::RECORDING)
        {
            Clear();
            return;
        }

        const uint16_t last = MaxPass();
        if(last <= 1)
        {
            Clear();
            return;
        }

        size_t w = 0;
        for(size_t r = 0; r < num_events_; r++)
        {
            Event e = events_[r];
            if(e.pass == last)
            {
                if(!(e.flags & kFlagErased))
                    continue; // a note of the undone pass: gone
                // a note the undone pass erased: back, in its own layer
                e.flags &= static_cast<uint8_t>(~kFlagErased);
                e.pass    = static_cast<uint16_t>(e.rec_odo);
                e.rec_odo = odo_ - static_cast<uint32_t>(len_);
            }
            events_[w++] = e;
        }
        num_events_ = w;
        full_       = false;
        pass_       = last - 1;
        if(state_ == State::OVERDUB)
            state_ = State::PLAYING;
    }

    /** Clear the loop. (Notes you're playing by hand carry on.) */
    void Clear()
    {
        AllNotesOff();
        ClearAll();
    }

    // ---------------- step view edits ----------------

    /** Make sure there's a loop to edit. With none, create an empty 1-bar loop and
     *  play it. If the transport is already running, the loop started at the last
     *  bar line (phase = units since it); otherwise it starts now, as a first note
     *  would (realigning the internal clock). */
    void EnsureLoop(int32_t phase)
    {
        if(state_ != State::EMPTY && state_ != State::ARMED)
            return;
        if(running_)
        {
            pass_ = 1;
            pos_  = phase < 0 ? 0 : Mod(phase, kUnitsPerBar);
        }
        else
            BeginFirstPass();
        num_events_          = 0;
        full_                = false;
        len_                 = kUnitsPerBar;
        state_               = State::PLAYING;
        scan_from_           = pos_ - 1;
        overdub_after_close_ = false;
    }

    /** Start a new undo layer for the edit(s) that follow. */
    void BeginEditPass() { pass_ = MaxPass() + 1; }

    /** Any note of this engine starting in [from, to)? */
    bool HasNotesIn(uint8_t engine, int32_t from, int32_t to) const
    {
        for(size_t i = 0; i < num_events_; i++)
        {
            const Event &e = events_[i];
            if(e.engine != engine || (e.flags & kFlagErased))
                continue;
            const int32_t q = Quantized(e);
            if(q >= from && q < to)
                return true;
        }
        return false;
    }

    /** Erase this engine's notes starting in [from, to). They're kept, silent, so
     *  Undo can bring them back. Returns how many. */
    size_t EraseRange(uint8_t engine, int32_t from, int32_t to)
    {
        size_t n = 0;
        for(size_t i = 0; i < num_events_; i++)
        {
            Event &e = events_[i];
            if(e.engine != engine || (e.flags & kFlagErased))
                continue;
            const int32_t q = Quantized(e);
            if(q < from || q >= to)
                continue;
            e.rec_odo = e.pass; // remembered for Undo
            e.pass    = pass_;
            e.flags |= kFlagErased;
            n++;
        }
        return n;
    }

    /** Add a note at a position in the loop (a step start), dur_us long. It plays
     *  when the playhead gets there. Needs a loop (see EnsureLoop). */
    bool InsertNote(uint8_t  engine,
                    int8_t   key,
                    int8_t   nn,
                    uint8_t  note,
                    int32_t  pos,
                    uint32_t dur_us,
                    uint8_t  grid)
    {
        if(state_ == State::EMPTY || state_ == State::ARMED)
            return false;
        if(num_events_ >= kMaxEvents)
        {
            full_ = true;
            return false;
        }
        if(len_ > 0)
            pos = Mod(pos, len_);
        Event &e  = events_[num_events_++];
        e.pos     = pos;
        e.rec_odo = odo_ - static_cast<uint32_t>(len_); // never "just played live"
        e.dur_us  = dur_us < kMinNoteUs ? kMinNoteUs : dur_us;
        e.key     = key;
        e.nn      = nn;
        e.engine  = engine;
        e.note    = note;
        e.pass    = pass_;
        e.flags   = kFlagSnapped;
        e.grid    = grid;
        return true;
    }

    /** One bit per step: bit i set if a note of this engine starts in
     *  [from + i * step_units, from + (i + 1) * step_units). steps <= 32. */
    uint32_t StepMask(uint8_t engine, int32_t from, int32_t step_units, int steps) const
    {
        if(step_units <= 0 || steps <= 0)
            return 0;
        if(steps > 32)
            steps = 32;
        const int32_t to = from + step_units * steps;
        uint32_t      m  = 0;
        for(size_t i = 0; i < num_events_; i++)
        {
            const Event &e = events_[i];
            if(e.engine != engine || (e.flags & kFlagErased))
                continue;
            const int32_t q = Quantized(e);
            if(q < from || q >= to)
                continue;
            m |= 1u << ((q - from) / step_units);
        }
        return m;
    }

    // ---------------- queries ----------------

    State    GetState() const { return state_; }
    bool     IsRunning() const { return running_; }
    bool     HasLoop() const { return len_ > 0 && state_ != State::RECORDING; }
    bool     IsFull() const { return full_; }
    int32_t  GetLength() const { return len_; }
    int32_t  GetPosition() const { return pos_; }
    size_t   GetNumEvents() const { return num_events_; }
    const Event &GetEvent(size_t i) const { return events_[i]; }

    /** Notes played by hand that LoopCore is sounding or about to sound. */
    size_t GetNumLiveNotes() const
    {
        size_t n = 0;
        for(const auto &ln : live_notes_)
            if(ln.active)
                n++;
        return n;
    }

    /** True once after the first note in free-clock mode, so the clock can realign. */
    bool ConsumeClockResetRequest()
    {
        const bool r         = request_clock_reset_;
        request_clock_reset_ = false;
        return r;
    }

    /** Where an event plays within the loop. */
    int32_t Quantized(const Event &e) const
    {
        int32_t p = e.pos;
        if(!(e.flags & kFlagSnapped))
            p = Pull(p, kGridUnits[e.grid < kNumGrids ? e.grid : kGridOff]);
        if(len_ > 0 && p >= len_)
            p -= len_; // a grid line at the loop end belongs to the downbeat
        return p;
    }

  private:
    struct PendingOff
    {
        uint32_t off_us;
        int8_t   key;
        uint8_t  engine;
        uint8_t  note;
        bool     from_midi;
        bool     active;
    };

    /** A note played by hand that LoopCore is sounding, or will sound on a grid line. */
    struct LiveNote
    {
        Event    ev;        // key, nn, engine, note, flags (the rest is unused)
        uint32_t press_us;  // when the key went down
        uint32_t start_odo; // odometer value at which it sounds (while waiting)
        uint32_t start_us;  // when it started sounding
        uint32_t held_us;   // how long the key was held (once released)
        uint32_t off_us;    // when it stops (once released and sounding)
        uint32_t seq;       // press order
        bool     sounding;
        bool     released;
        bool     active;
    };

    static int32_t Mod(int32_t a, int32_t m)
    {
        int32_t r = a % m;
        return r < 0 ? r + m : r;
    }

    /** Nearest multiple of g (works for negative positions too). */
    static int32_t RoundToGrid(int32_t p, int32_t g)
    {
        const int32_t f = p + g / 2;
        const int32_t d = f >= 0 ? f / g : -((-f + g - 1) / g); // floor(f / g)
        return d * g;
    }

    /** Next multiple of g at or after p (works for negative positions too). */
    static int32_t CeilToGrid(int32_t p, int32_t g)
    {
        const int32_t d = p >= 0 ? (p + g - 1) / g : -((-p) / g); // ceil(p / g)
        return d * g;
    }

    /** Pull a position toward grid line `line` by the quantize strength.
     *  g = 0 (grid off): unchanged. */
    int32_t PullTo(int32_t p, int32_t line, int32_t g) const
    {
        if(g <= 0 || strength_ <= 0)
            return p;
        return p + ((line - p) * strength_) / 100;
    }

    /** Pull a position toward the nearest line of grid g (playback quantize). */
    int32_t Pull(int32_t p, int32_t g) const
    {
        return g > 0 ? PullTo(p, RoundToGrid(p, g), g) : p;
    }

    /** Pull a position toward the next line of grid g, never earlier (live quantize). */
    int32_t PullForward(int32_t p, int32_t g) const
    {
        return g > 0 ? PullTo(p, CeilToGrid(p, g), g) : p;
    }

    uint16_t MaxPass() const
    {
        uint16_t m = 0;
        for(size_t i = 0; i < num_events_; i++)
            if(events_[i].pass > m)
                m = events_[i].pass;
        return m;
    }

    void ClearAll()
    {
        state_       = State::EMPTY;
        num_events_  = 0;
        len_         = 0;
        pos_         = 0;
        scan_from_   = -1;
        pass_        = 0;
        full_        = false;
        overdub_after_close_ = false;
    }

    void BeginFirstPass()
    {
        state_   = State::RECORDING;
        pass_    = 1;
        len_     = 0;
        running_ = true;

        if(sync_)
        {
            // Following an external clock: snap the loop start to the nearest beat.
            // Played late -> the loop started a moment ago (positive position).
            // Played early -> the loop starts on the coming beat (negative position).
            const int32_t ph = beat_phase_;
            pos_ = ph < kUnitsPerBeat / 2 ? ph : ph - kUnitsPerBeat;
        }
        else
        {
            // Internal clock: this note is beat 1, exactly.
            pos_                 = 0;
            request_clock_reset_ = true;
        }
        scan_from_ = -1;
    }

    void CloseFirstPass(uint32_t press_timeline)
    {
        // Position at the moment the key went down, not when it was released
        int32_t back = static_cast<int32_t>(last_timeline_ - press_timeline);
        if(back < 0)
            back = 0;
        if(back > kUnitsPerBar)
            back = kUnitsPerBar;
        const int32_t close_pos = pos_ - back;

        int32_t bars = close_pos <= 0 ? 0 : (close_pos + kUnitsPerBar / 2) / kUnitsPerBar;
        if(bars < 1)
            bars = 1;
        if(bars > kMaxBars)
            bars = kMaxBars;
        len_ = bars * kUnitsPerBar;

        if(pos_ >= len_)
        {
            // Closed after the bar line: the loop already wrapped a moment ago.
            // Carry on from the matching position; anything played in the overshoot
            // was heard live and wraps around to the start for the next pass.
            FinalizeFirstPass();
            pos_       = Mod(pos_, len_);
            scan_from_ = pos_;
        }
        // Otherwise keep recording until the playhead reaches the bar line
        // (handled in Advance), so notes played in the remaining time are kept.
    }

    void FinalizeFirstPass()
    {
        for(size_t i = 0; i < num_events_; i++)
            events_[i].pos = Mod(events_[i].pos, len_);
        state_ = State::PLAYING;
        if(overdub_after_close_)
        {
            overdub_after_close_ = false;
            state_               = State::OVERDUB;
            pass_++;
        }
    }

    void Advance(int32_t delta)
    {
        odo_ += static_cast<uint32_t>(delta);
        StartDueLiveNotes();

        if(state_ == State::RECORDING)
        {
            pos_ += delta;
            if(len_ == 0 && pos_ >= kMaxLength)
                len_ = kMaxLength; // hit the length cap: close at the cap
            if(len_ > 0 && pos_ >= len_)
            {
                const int32_t over = Mod(pos_ - len_, len_);
                FinalizeFirstPass();
                Fire(-1, over); // the downbeat lands right on time
                pos_       = over;
                scan_from_ = over;
            }
            return;
        }

        if(state_ != State::PLAYING && state_ != State::OVERDUB)
            return; // EMPTY / ARMED: nothing to play

        int32_t to = pos_ + delta;
        if(to >= len_)
        {
            Fire(scan_from_, len_ - 1);
            to = Mod(to - len_, len_);
            Fire(-1, to);
        }
        else
        {
            Fire(scan_from_, to);
        }
        pos_       = to;
        scan_from_ = to;
    }

    /** Play every event whose quantized position is in (from, to]. */
    void Fire(int32_t from, int32_t to)
    {
        if(to <= from)
            return;
        const uint32_t fresh_window = static_cast<uint32_t>(len_ / 2);
        for(size_t i = 0; i < num_events_; i++)
        {
            const Event &e = events_[i];
            if(e.flags & (kFlagHeld | kFlagErased))
                continue; // still being held (length unknown), or erased
            const int32_t q = Quantized(e);
            if(q <= from || q > to)
                continue;
            // Don't replay a note right after it was played live
            // (e.g. it was played a moment before the position it was recorded at).
            if(odo_ - e.rec_odo < fresh_window)
                continue;
            PlayEvent(e);
        }
    }

    void PlayEvent(const Event &e)
    {
        if(out_)
            out_->LoopNoteOn(e);
        ScheduleOff(e);
    }

    void ScheduleOff(const Event &e)
    {
        const uint32_t off = now_us_ + e.dur_us;
        const bool     midi = e.flags & kFlagFromMidi;

        // Same pitch already scheduled: the voice was just retriggered, so it now
        // follows the new note's length. (A voice is a pitch on an engine.)
        for(auto &p : pending_)
        {
            if(p.active && p.engine == e.engine && p.note == e.note)
            {
                p.off_us = off;
                p.key    = e.key;
                return;
            }
        }
        for(auto &p : pending_)
        {
            if(!p.active)
            {
                p = {off, e.key, e.engine, e.note, midi, true};
                return;
            }
        }
        // No free slot: release the note due soonest and reuse its slot
        PendingOff *soonest = &pending_[0];
        for(auto &p : pending_)
            if(static_cast<int32_t>(p.off_us - soonest->off_us) < 0)
                soonest = &p;
        if(out_)
            out_->LoopNoteOff(soonest->engine, soonest->key, soonest->note, soonest->from_midi);
        *soonest = {off, e.key, e.engine, e.note, midi, true};
    }

    void ServiceNoteOffs()
    {
        for(auto &p : pending_)
        {
            if(p.active && static_cast<int32_t>(now_us_ - p.off_us) >= 0)
            {
                p.active = false;
                if(out_)
                    out_->LoopNoteOff(p.engine, p.key, p.note, p.from_midi);
            }
        }
    }

    void AllNotesOff()
    {
        for(auto &p : pending_)
        {
            if(p.active)
            {
                p.active = false;
                if(out_)
                    out_->LoopNoteOff(p.engine, p.key, p.note, p.from_midi);
            }
        }
    }

    // ---------------- live notes ----------------

    void AddLiveNote(uint8_t engine, int8_t key, int8_t nn, uint8_t note, bool from_midi, int32_t wait)
    {
        // A key can only be down once. A note on it that's still held lost its
        // release somewhere (or a MIDI note-on was repeated): let it go first.
        for(auto &n : live_notes_)
            if(n.active && !n.released && n.ev.key == key)
                EndLiveNote(n);

        LiveNote *slot = nullptr;
        for(auto &n : live_notes_)
        {
            if(!n.active)
            {
                slot = &n;
                break;
            }
        }
        if(!slot)
        {
            // All in use (very unlikely): let go of the oldest
            slot = &live_notes_[0];
            for(auto &n : live_notes_)
                if(static_cast<int32_t>(n.seq - slot->seq) < 0)
                    slot = &n;
            EndLiveNote(*slot);
        }

        LiveNote &n = *slot;
        n.ev        = Event{};
        n.ev.key    = key;
        n.ev.nn     = nn;
        n.ev.engine = engine;
        n.ev.note   = note;
        n.ev.flags  = from_midi ? kFlagFromMidi : 0;
        n.press_us  = now_us_;
        n.start_odo = odo_ + static_cast<uint32_t>(wait > 0 ? wait : 0);
        n.start_us  = 0;
        n.held_us   = 0;
        n.off_us    = 0;
        n.seq       = ++live_seq_;
        n.sounding  = false;
        n.released  = false;
        n.active    = true;
        if(wait <= 0)
            StartLiveNote(n);
    }

    void StartLiveNote(LiveNote &n)
    {
        // Another live note already sounding on this voice (same pitch, engine):
        // this one retriggers the voice and takes it over, so the older note's
        // release mustn't cut it short. (Looped notes work the same way.)
        for(auto &o : live_notes_)
            if(&o != &n && o.active && o.sounding && o.ev.engine == n.ev.engine
               && o.ev.note == n.ev.note)
                o.active = false;

        n.sounding = true;
        n.start_us = now_us_;
        if(n.released) // let go before its grid line: plays as long as it was held
            n.off_us = now_us_ + n.held_us;
        if(out_)
            out_->LiveNoteOn(n.ev);
    }

    /** Stop a live note now (if it's sounding) and free its slot. */
    void EndLiveNote(LiveNote &n)
    {
        if(n.active && n.sounding && out_)
            out_->LiveNoteOff(n.ev);
        n.active = false;
    }

    /** Sound the live notes whose grid line the playhead has reached. Oldest press
     *  first, so when two presses of one key land together the newer one sets
     *  the length (as with looped notes). */
    void StartDueLiveNotes()
    {
        for(;;)
        {
            LiveNote *next = nullptr;
            for(auto &n : live_notes_)
                if(n.active && !n.sounding && static_cast<int32_t>(odo_ - n.start_odo) >= 0
                   && (!next || static_cast<int32_t>(n.seq - next->seq) < 0))
                    next = &n;
            if(!next)
                return;
            StartLiveNote(*next);
        }
    }

    /** Stop the live notes whose time is up. */
    void ReleaseDueLiveNotes()
    {
        for(auto &n : live_notes_)
            if(n.active && n.sounding && n.released
               && static_cast<int32_t>(now_us_ - n.off_us) >= 0)
                EndLiveNote(n);
    }

    /** The grid stopped or started over while live notes wait for a line: a note
     *  whose key is still down sounds now; one already let go is dropped. */
    void ResolveWaitingLiveNotes()
    {
        for(auto &n : live_notes_)
        {
            if(!n.active || n.sounding)
                continue;
            if(n.released)
                n.active = false;
            else
                StartLiveNote(n);
        }
    }

    Output *out_;

    Event      events_[kMaxEvents];
    size_t     num_events_;
    PendingOff pending_[kMaxPendingOffs];
    LiveNote   live_notes_[kMaxLiveNotes];
    uint32_t   live_seq_;

    State    state_;
    bool     running_;
    bool     full_;
    int32_t  len_;       // loop length in units (0 = not closed yet)
    int32_t  pos_;       // playhead within the loop
    int32_t  scan_from_; // last position already scanned (exclusive), -1 = include 0
    uint16_t pass_;
    uint32_t odo_;       // playhead odometer: total units travelled while running

    int     record_grid_;
    int32_t strength_;
    bool    live_;

    bool     overdub_after_close_;

    bool     sync_;
    int32_t  beat_phase_;
    bool     request_clock_reset_;

    bool     have_time_;
    uint32_t last_timeline_;
    uint32_t now_us_;
};

} // namespace looper
