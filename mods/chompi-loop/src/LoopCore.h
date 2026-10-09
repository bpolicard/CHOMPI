/** LoopCore
 *  Hardware-independent logic for the polyphonic note-event looper.
 *
 *  Everything here is plain C++ with no libDaisy dependencies, so it can be
 *  compiled and tested on a computer. NoteLooper.h connects it to CHOMPI.
 *
 *  Positions are measured in "units": the 24 PPQN clock subdivided by 32,
 *  so one beat = 768 units. 768 divides evenly by every quantize grid we use
 *  (including triplets), which keeps all the timing math in integers.
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

struct Event
{
    int32_t  pos;     // raw (unquantized) position from loop start, in units
    uint32_t rec_odo; // playhead odometer when recorded (prevents instant double hits)
    uint32_t dur_us;  // note length in µs. While kFlagHeld is set: the note-on time.
    int8_t   key;     // engine voice id (Hardware::SwId value)
    int8_t   nn;      // transpose in semitones
    uint8_t  engine;  // 0 = chroma, 1 = slice
    uint8_t  note;    // MIDI note number, for MIDI out
    uint16_t pass;    // recording pass number, for undo
    uint8_t  flags;
    uint8_t  grid;    // quantize grid (index into kGridUnits) when this note was played
};

/** Where the looper sends its notes. NoteLooper implements this. */
class Output
{
  public:
    virtual ~Output() {}
    virtual void LoopNoteOn(const Event &ev) = 0;
    virtual void
    LoopNoteOff(uint8_t engine, int8_t key, uint8_t note, bool from_midi)
        = 0;
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

    /** true: quantize note starts as they're recorded (default).
     *  false: keep the played timing and quantize on playback. */
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

        ServiceNoteOffs();
    }

    // ---------------- performance input ----------------

    void NoteOn(uint8_t engine, int8_t key, int8_t nn, uint8_t note, bool from_midi)
    {
        if(state_ == State::ARMED)
            BeginFirstPass();

        const bool recording
            = state_ == State::RECORDING || (state_ == State::OVERDUB && running_);
        if(!recording)
            return;

        if(num_events_ >= kMaxEvents)
        {
            full_ = true;
            return;
        }

        int32_t p     = pos_;
        uint8_t flags = kFlagHeld | (from_midi ? kFlagFromMidi : 0);
        if(live_)
        {
            // Live quantize: the note sounds right away, but its start is stored
            // on the grid. (Its length stays exactly as played.)
            p = Pull(p, kGridUnits[record_grid_]);
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

    void NoteOff(int8_t key)
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
                return;
            }
        }
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
        if(state_ == State::PLAYING || state_ == State::OVERDUB)
        {
            if(start_pos > 0)
                start_pos = 0;
            pos_       = start_pos;
            scan_from_ = start_pos - 1; // include notes sitting exactly on the downbeat
        }
    }

    /** Transport stop. Pauses everything and releases looper notes. */
    void Stop()
    {
        running_ = false;
        AllNotesOff();
    }

    /** Remove the most recent recording pass. Undoing the first take clears the loop. */
    void Undo()
    {
        if(state_ == State::ARMED || state_ == State::RECORDING)
        {
            Clear();
            return;
        }

        uint16_t last = 0;
        for(size_t i = 0; i < num_events_; i++)
            if(events_[i].pass > last)
                last = events_[i].pass;

        if(last <= 1)
        {
            Clear();
            return;
        }

        size_t w = 0;
        for(size_t r = 0; r < num_events_; r++)
            if(events_[r].pass != last)
                events_[w++] = events_[r];
        num_events_ = w;
        full_       = false;
        pass_       = last - 1;
        if(state_ == State::OVERDUB)
            state_ = State::PLAYING;
    }

    void Clear()
    {
        AllNotesOff();
        ClearAll();
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

    /** Pull a position toward grid g by the quantize strength. g = 0: unchanged. */
    int32_t Pull(int32_t p, int32_t g) const
    {
        if(g <= 0 || strength_ <= 0)
            return p;
        return p + ((RoundToGrid(p, g) - p) * strength_) / 100;
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
            if(e.flags & kFlagHeld)
                continue; // still being held: length unknown
            const int32_t q = Quantized(e);
            if(q <= from || q > to)
                continue;
            // Don't replay a note right after it was played live
            // (e.g. quantized a few ms later than it was played).
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

        // Same key already scheduled: the voice was just retriggered, so it now
        // follows the new note's length.
        for(auto &p : pending_)
        {
            if(p.active && p.key == e.key && p.engine == e.engine)
            {
                p.off_us = off;
                p.note   = e.note;
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

    Output *out_;

    Event      events_[kMaxEvents];
    size_t     num_events_;
    PendingOff pending_[kMaxPendingOffs];

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
