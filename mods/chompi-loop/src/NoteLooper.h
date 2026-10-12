/** NoteLooper
 *  Connects LoopCore (the looper logic) to CHOMPI: the sample engines, MIDI out,
 *  the shared clock timeline, options.json, and the CHOMPI / Loop key gestures.
 *
 *  Plain notes you play (keyboard or MIDI in, with no arpeggiator involved) are
 *  sounded by the looper rather than sent straight to the engines, so that with
 *  live quantize they can wait for the next grid line. They still reach the engines
 *  in the same audio block as before when they don't have to wait.
 *
 *  It also holds the Chroma key transpose (the stepped pitch knob, in fourths and
 *  fifths) and maps every Chroma note to the key that plays that pitch at the current
 *  transpose, so the key LEDs follow the pitches as you transpose.
 *
 *  Threading: two contexts talk to the looper.
 *   - Main loop (UI pages): Queue...() calls only. They go through a FIFO.
 *   - Audio callback (ui.GenerateEvents, MidiManager, AudioCallback): everything else,
 *     called directly. Process() runs once per audio block in the same context.
 */
#pragma once
#include "daisy.h"
#include "EngineBase.h"
#include "hardware.h"
#include "clockManager.h"
#include "OptionsManager.h"
#include "LoopCore.h"

static_assert(looper::kUnitsPerTick == kTimelineUnitsPerTick,
              "looper and clock timeline units must match");

struct LooperInput
{
    enum class Type : uint8_t
    {
        NOTE_ON,
        NOTE_OFF,
        PLAY_TOGGLE,
    };
    Type    type;
    uint8_t engine;
    int8_t  key;
    int8_t  nn;
    uint8_t note;
    bool    sound; // NOTE_ON: the looper sounds the note (and stops it on NOTE_OFF)
};

class NoteLooper : public looper::Output
{
  public:
    static constexpr uint32_t kTapMaxMs       = 400;  // longer than this isn't a tap
    static constexpr uint32_t kClearHoldMs    = 1000; // shift + hold Loop
    static constexpr uint32_t kGridShowMs     = 1500; // grid display after a change

    void Init(BaseEngine           **engines,
              chompi::Hardware      *hw,
              clockManager          *cm,
              chompi::OptionsManager *options)
    {
        engines_ = engines;
        hw_      = hw;
        cm_      = cm;

        midi_ch_[0]   = options->midi_ch_out_chroma;
        midi_ch_[1]   = options->midi_ch_out_slice;
        transport_out_ = options->transport_type == 0 || options->transport_type == 1;

        core_.Init(this);
        core_.SetStrength(options->loop_quantize_strength);
        core_.SetLiveQuantize(options->loop_live_quantize);
        grid_home_       = GridIndexFromNotes(options->loop_quantize_grid);
        grid_offset_     = 0;
        grid_changed_ms_ = 0;
        grid_shown_      = false;
        core_.SetRecordGrid(ActiveGridIndex());

        chompi_down_ = chompi_candidate_ = chompi_other_ = false;
        loop_down_ = loop_shift_ = loop_clear_fired_ = false;

        initialized_ = true;
    }

    /** True once the looper is set up. From then on it sounds the plain notes you
     *  play (no arpeggiator), so they can wait for the grid: callers hand those notes
     *  to it with sound = true instead of starting / stopping the engines themselves. */
    bool SoundsLiveNotes() const { return initialized_; }

    // ------------- main loop side (UI pages) -------------

    /** A key press. sound: the looper sounds it (and stops it on the release);
     *  otherwise it's only recorded (something else is sounding it). */
    void QueueNoteOn(uint8_t engine, int key, float nn, uint8_t note, bool sound)
    {
        LooperInput in{LooperInput::Type::NOTE_ON,
                       engine,
                       static_cast<int8_t>(key),
                       static_cast<int8_t>(nn),
                       note,
                       sound};
        input_fifo_.PushBack(in);
    }

    void QueueNoteOff(int key)
    {
        LooperInput in{LooperInput::Type::NOTE_OFF, 0, static_cast<int8_t>(key), 0, 0, false};
        input_fifo_.PushBack(in);
    }

    void QueuePlayToggle()
    {
        LooperInput in{LooperInput::Type::PLAY_TOGGLE, 0, 0, 0, 0, false};
        input_fifo_.PushBack(in);
    }

    /** Press + turn the tempo knob: scroll through every grid
     *  (1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, off). */
    void ScrollGrid(int turns)
    {
        int h = grid_home_ + turns;
        h     = h < 0 ? 0 : (h >= looper::kNumGrids ? looper::kNumGrids - 1 : h);
        grid_home_   = static_cast<int8_t>(h);
        grid_offset_ = 0;
        MarkGridChanged();
        if(cm_)
            cm_->cancelTap(); // a press used for turning shouldn't count toward tap tempo
    }

    /** Shift + turn the tempo knob: hop to the nearest straight / triplet grid
     *  just above or below the scrolled-to grid, and back. */
    void FlipGrid(int dir)
    {
        int o = grid_offset_ + (dir > 0 ? 1 : -1);
        o     = o < -1 ? -1 : (o > 1 ? 1 : o);
        const int a = grid_home_ + o;
        if(a >= 0 && a < looper::kNumGrids)
            grid_offset_ = static_cast<int8_t>(o);
        MarkGridChanged();
    }

    /** Grid used for new notes (index into looper::kGridUnits) */
    int ActiveGridIndex() const
    {
        const int a = grid_home_ + grid_offset_;
        return a < 0 ? 0 : (a >= looper::kNumGrids ? looper::kNumGrids - 1 : a);
    }
    int HomeGridIndex() const { return grid_home_; }

    /** True for a moment after the grid changes, so the keys can show it */
    bool ShowGrid() const
    {
        return grid_shown_ && daisy::System::GetNow() - grid_changed_ms_ < kGridShowMs;
    }

    /** Where we are within the bar (0 .. kUnitsPerBar - 1), for the tempo and grid
     *  LEDs. Follows the loop when there is one, otherwise the clock since it was
     *  started. Updated once per audio block. */
    int32_t BarPhaseUnits() const { return bar_phase_; }

    // ------------- Chroma key transpose (main loop) -------------
    //
    // In Chroma the stepped pitch knob moves the keys instead of re-pitching the
    // sample: a key plays (and sends over MIDI) its note plus the transpose. Steps
    // alternate a fifth and a fourth, like TEMPO's stepped pitch, so two clicks make
    // an octave: 0, +7, +12, +19, +24 ... and 0, -5, -12, -17, -24 ..., up to 4 octaves
    // either way. Only notes you play from then on are affected.

    static constexpr int kMaxTransposeSteps = 8;  // 4 octaves
    static constexpr int kMaxTranspose      = 48; // semitones
    static constexpr int kKnobTurnsPerStep  = 4;  // detents per step, as TEMPO's knob

    int Transpose() const { return transpose_; }

    /** Set the transpose outright (from a preset or an A / B state). */
    void SetTranspose(int semis)
    {
        semis = semis < -kMaxTranspose ? -kMaxTranspose : (semis > kMaxTranspose ? kMaxTranspose : semis);
        transpose_step_ = StepForSemis(semis);
        transpose_      = static_cast<int8_t>(SemisForStep(transpose_step_));
        knob_turns_     = 0;
    }

    /** Turn the stepped pitch knob in Chroma. */
    void StepTranspose(int turns)
    {
        knob_turns_ += turns;
        while(knob_turns_ >= kKnobTurnsPerStep)
        {
            knob_turns_ -= kKnobTurnsPerStep;
            if(transpose_step_ < kMaxTransposeSteps)
                transpose_step_++;
        }
        while(knob_turns_ <= -kKnobTurnsPerStep)
        {
            knob_turns_ += kKnobTurnsPerStep;
            if(transpose_step_ > -kMaxTransposeSteps)
                transpose_step_--;
        }
        transpose_ = static_cast<int8_t>(SemisForStep(transpose_step_));
    }

    // ------------- audio callback side -------------

    /** A MIDI note in. sound: the looper sounds it (and stops it on the note-off);
     *  otherwise it's only recorded (something else is sounding it). */
    void MidiNoteOn(uint8_t engine, int key, float nn, uint8_t note, bool sound)
    {
        if(initialized_)
            core_.NoteOn(engine,
                         static_cast<int8_t>(key),
                         static_cast<int8_t>(nn),
                         note,
                         true,
                         sound);
    }

    /** A MIDI note off. Returns true if the looper is sounding that note and stops
     *  it itself; false if the caller should stop it. */
    bool MidiNoteOff(int key)
    {
        return initialized_ && core_.NoteOff(static_cast<int8_t>(key));
    }

    /** External MIDI Start / Stop (only called when following MIDI transport). */
    void ExternalStart()
    {
        if(!initialized_)
            return;
        // The downbeat is the first clock tick after Start: hold the loop's
        // position back by the time remaining until that tick.
        const uint32_t units = cm_->getTimelineUnits();
        const uint32_t into  = units % kTimelineUnitsPerTick;
        core_.Start(-static_cast<int32_t>(kTimelineUnitsPerTick - into));
    }
    void ExternalStop()
    {
        if(initialized_)
            core_.Stop();
    }

    /** CHOMPI key edge.
     *  shift_mode: mode switch down (the key is shift).
     *  opened_menu: this press opened the shift menu from the normal page
     *  (false while confirming a save / copy / erase, so those never count as taps). */
    void OnChompiKey(bool pressed, bool shift_mode, bool opened_menu)
    {
        if(!initialized_)
            return;
        if(pressed)
        {
            chompi_down_      = true;
            chompi_shift_     = shift_mode;
            chompi_candidate_ = shift_mode && opened_menu;
            chompi_other_     = false;
            chompi_press_ms_  = daisy::System::GetNow();
            chompi_press_pos_ = cm_->getTimelineUnits();
        }
        else
        {
            chompi_down_ = false;
            if(chompi_candidate_ && !chompi_other_
               && daisy::System::GetNow() - chompi_press_ms_ < kTapMaxMs)
            {
                core_.ChompiTap(chompi_press_pos_);
            }
            chompi_candidate_ = false;
        }
    }

    /** Loop key edge. */
    void OnLoopKey(bool pressed)
    {
        if(!initialized_)
            return;
        if(pressed)
        {
            loop_down_ = true;
            if(chompi_down_ && chompi_shift_)
            {
                // shift + Loop: undo (tap) or clear (hold)
                chompi_other_     = true;
                loop_shift_       = true;
                loop_clear_fired_ = false;
                loop_press_ms_    = daisy::System::GetNow();
            }
        }
        else
        {
            loop_down_ = false;
            if(loop_shift_ && !loop_clear_fired_)
                core_.Undo();
            loop_shift_ = false;
        }
    }

    /** Any other key, encoder press, or encoder turn (cancels a CHOMPI tap). */
    void OnOtherInput()
    {
        if(chompi_down_)
            chompi_other_ = true;
    }

    /** Once per audio block, before the engines' Prepare(). */
    void Process()
    {
        if(!initialized_)
            return;

        const bool     sync     = cm_->getClockMode() == SYNC;
        const uint32_t timeline = cm_->getTimelineUnits();
        core_.SetSyncInfo(sync, cm_->getBeatPhaseUnits());
        core_.SetRecordGrid(ActiveGridIndex());
        core_.Process(timeline, daisy::System::GetUs());

        while(!input_fifo_.IsEmpty())
        {
            const LooperInput in = input_fifo_.PopFront();
            switch(in.type)
            {
                case LooperInput::Type::NOTE_ON:
                    core_.NoteOn(in.engine, in.key, in.nn, in.note, false, in.sound);
                    break;
                case LooperInput::Type::NOTE_OFF: core_.NoteOff(in.key); break;
                case LooperInput::Type::PLAY_TOGGLE: TogglePlay(); break;
            }
        }

        // first note in free-clock mode: realign the clock's beat to it
        if(core_.ConsumeClockResetRequest())
        {
            cm_->setNow(0);
            cm_->setNow(1);
            cm_->setNow(2);
            clock_origin_units_ = timeline;
        }

        // bar phase for the LEDs: the loop's position, or the clock since it started
        {
            const looper::State st = core_.GetState();
            int32_t p;
            if(st == looper::State::RECORDING || st == looper::State::PLAYING
               || st == looper::State::OVERDUB)
                p = core_.GetPosition();
            else
                p = static_cast<int32_t>(
                    timeline - (sync ? cm_->getSongOriginUnits() : clock_origin_units_));
            p %= looper::kUnitsPerBar;
            bar_phase_ = p < 0 ? p + looper::kUnitsPerBar : p;
        }

        // shift + hold Loop: clear
        if(loop_shift_ && loop_down_ && !loop_clear_fired_
           && daisy::System::GetNow() - loop_press_ms_ >= kClearHoldMs)
        {
            core_.Clear();
            loop_clear_fired_ = true;
        }
    }

    // ------------- state for LEDs etc. -------------

    bool          IsRunning() const { return initialized_ && core_.IsRunning(); }
    looper::State GetState() const { return core_.GetState(); }
    bool          HasLoop() const { return core_.HasLoop(); }
    bool          IsFull() const { return core_.IsFull(); }

    // ------------- looper::Output -------------

    void LoopNoteOn(const looper::Event &e) override
    {
        const uint8_t eng = e.engine > 1 ? 1 : e.engine;
        engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,
                                                        static_cast<float>(e.nn),
                                                        StartVoice(eng, e.key, e.note),
                                                        127.f,
                                                        KeyRequest::Source::SEQUENCER));
        if(!(e.flags & looper::kFlagFromMidi))
            hw_->queueMidiNote(midi_ch_[eng], e.note, 127, NoteOn);
    }

    void LoopNoteOff(uint8_t engine, int8_t key, uint8_t note, bool from_midi) override
    {
        const uint8_t eng = engine > 1 ? 1 : engine;
        engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,
                                                        0.f,
                                                        VoiceOf(eng, key, note),
                                                        127.f,
                                                        KeyRequest::Source::SEQUENCER));
        if(!from_midi)
            hw_->queueMidiNote(midi_ch_[eng], note, 127, NoteOff);
    }

    /** A note you played starts sounding. Sent as Source::USER, exactly like a key
     *  press: the engines keep a voice going while either your hand (USER) or the
     *  loop (SEQUENCER) wants it, so a looped note ending can't cut a held key. */
    void LiveNoteOn(const looper::Event &e) override
    {
        const uint8_t eng = e.engine > 1 ? 1 : e.engine;
        engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,
                                                        static_cast<float>(e.nn),
                                                        StartVoice(eng, e.key, e.note),
                                                        127.f,
                                                        KeyRequest::Source::USER));
        if(!(e.flags & looper::kFlagFromMidi))
            hw_->queueMidiNote(midi_ch_[eng], e.note, 127, NoteOn);
    }

    void LiveNoteOff(const looper::Event &e) override
    {
        const uint8_t eng = e.engine > 1 ? 1 : e.engine;
        engines_[eng]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,
                                                        static_cast<float>(e.nn),
                                                        VoiceOf(eng, e.key, e.note),
                                                        127.f,
                                                        KeyRequest::Source::USER));
        if(!(e.flags & looper::kFlagFromMidi))
            hw_->queueMidiNote(midi_ch_[eng], e.note, 127, NoteOff);
    }

  private:
    static constexpr int kLowestKeyNote  = 48; // MIDI note of key 1 (C3) in Chroma
    static constexpr int kHighestKeyNote = 72; // MIDI note of key 15 (C5)

    /** Transpose in semitones for a knob step: fifths and fourths, two steps an octave. */
    static int SemisForStep(int step)
    {
        const int m = step < 0 ? -step : step;
        const int t = 12 * (m / 2) + ((m & 1) ? (step > 0 ? 7 : 5) : 0);
        return step < 0 ? -t : t;
    }

    /** The knob step nearest a transpose in semitones (inverse of SemisForStep). */
    static int StepForSemis(int semis)
    {
        const int m = semis < 0 ? -semis : semis;
        const int s = 2 * (m / 12) + ((m % 12) ? 1 : 0);
        return semis < 0 ? -s : s;
    }

    /** The engine voice for a note. Slice: the key, as always. Chroma: the key that
     *  plays this pitch at the current transpose, so its LED lights; a pitch no key
     *  reaches right now gets a voice id of its own (no LED). */
    int VoiceForNote(uint8_t engine, int8_t key, uint8_t note) const
    {
        if(engine != 0)
            return key;
        // Hardware::SwId of the key for each MIDI note from C3 to C5 (same order as
        // ui.h's midi2key for those notes)
        static const int8_t kKeyForNote[kHighestKeyNote - kLowestKeyNote + 1]
            = {15, 7, 8, 12, 9, 10, 13, 11, 14, 16, 21, 17, 18, 22, 19, 23, 20, 24, 29, 25, 30, 26, 31, 27, 28};
        const int n = static_cast<int>(note) - transpose_;
        if(n >= kLowestKeyNote && n <= kHighestKeyNote)
            return kKeyForNote[n - kLowestKeyNote];
        return 200 + n + kMaxTranspose; // off the keyboard: unique, no LED (ids > 40)
    }

    /** Pick the voice for a note that starts now, and remember it for its note-off
     *  (the transpose may change in between). */
    int StartVoice(uint8_t engine, int8_t key, uint8_t note)
    {
        const int v = VoiceForNote(engine, key, note);
        if(engine == 0)
            chroma_voice_[note & 127] = static_cast<int16_t>(v);
        return v;
    }

    int VoiceOf(uint8_t engine, int8_t key, uint8_t note) const
    {
        return engine == 0 ? chroma_voice_[note & 127] : key;
    }

    /** options.json grid value (notes per whole note, 0 = off) -> grid index */
    static int8_t GridIndexFromNotes(uint8_t notes)
    {
        for(int i = 0; i < looper::kNumGrids; i++)
            if(looper::kGridNotes[i] == notes)
                return static_cast<int8_t>(i);
        return looper::kGridSixteenth;
    }

    void MarkGridChanged()
    {
        grid_changed_ms_ = daisy::System::GetNow();
        grid_shown_      = true;
    }

    void TogglePlay()
    {
        const bool start = !core_.IsRunning();
        if(start)
            core_.Start();
        else
            core_.Stop();

        if(cm_->getClockMode() == FREE)
        {
            if(start)
            {
                // restart the clock's beat so the delay and MIDI clock line up
                cm_->setNow(0);
                cm_->setNow(1);
                cm_->setNow(2);
                clock_origin_units_ = cm_->getTimelineUnits();
            }
            if(transport_out_)
                hw_->queueMidiTransport(midi_ch_[0], start);
        }
    }

    looper::LoopCore core_;

    BaseEngine       **engines_ = nullptr;
    chompi::Hardware  *hw_      = nullptr;
    clockManager      *cm_      = nullptr;

    daisy::FIFO<LooperInput, 64> input_fifo_;

    uint8_t midi_ch_[2];
    bool    transport_out_;
    bool    initialized_ = false;

    // quantize grid: set from the UI (main loop), read in the audio callback
    volatile int8_t grid_home_   = looper::kGridSixteenth;
    volatile int8_t grid_offset_ = 0;
    uint32_t        grid_changed_ms_ = 0;
    bool            grid_shown_      = false;

    // bar phase for the LEDs: set in the audio callback, read by the UI (main loop)
    volatile int32_t bar_phase_          = 0;
    uint32_t         clock_origin_units_ = 0; // timeline position the clock last started at

    // Chroma key transpose: set from the UI (main loop), read in the audio callback
    volatile int8_t transpose_      = 0; // semitones
    int             transpose_step_ = 0; // knob steps from 0
    int             knob_turns_     = 0; // detents toward the next step
    int16_t         chroma_voice_[128] = {}; // engine voice used for each Chroma note

    // CHOMPI key gesture
    bool     chompi_down_;
    bool     chompi_shift_;
    bool     chompi_candidate_;
    bool     chompi_other_;
    uint32_t chompi_press_ms_;
    uint32_t chompi_press_pos_;

    // Loop key gesture
    bool     loop_down_;
    bool     loop_shift_;
    bool     loop_clear_fired_;
    uint32_t loop_press_ms_;
};
