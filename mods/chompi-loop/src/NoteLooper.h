/** NoteLooper
 *  Connects LoopCore (the looper logic) to CHOMPI: the sample engines, MIDI out,
 *  the shared clock timeline, options.json, and the CHOMPI / Loop key gestures.
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

    // ------------- main loop side (UI pages) -------------

    void QueueNoteOn(uint8_t engine, int key, float nn, uint8_t note)
    {
        LooperInput in{LooperInput::Type::NOTE_ON,
                       engine,
                       static_cast<int8_t>(key),
                       static_cast<int8_t>(nn),
                       note};
        input_fifo_.PushBack(in);
    }

    void QueueNoteOff(int key)
    {
        LooperInput in{LooperInput::Type::NOTE_OFF, 0, static_cast<int8_t>(key), 0, 0};
        input_fifo_.PushBack(in);
    }

    void QueuePlayToggle()
    {
        LooperInput in{LooperInput::Type::PLAY_TOGGLE, 0, 0, 0, 0};
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

    // ------------- audio callback side -------------

    void MidiNoteOn(uint8_t engine, int key, float nn, uint8_t note)
    {
        if(initialized_)
            core_.NoteOn(engine, static_cast<int8_t>(key), static_cast<int8_t>(nn), note, true);
    }

    void MidiNoteOff(int key)
    {
        if(initialized_)
            core_.NoteOff(static_cast<int8_t>(key));
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

        core_.SetSyncInfo(cm_->getClockMode() == SYNC, cm_->getBeatPhaseUnits());
        core_.SetRecordGrid(ActiveGridIndex());
        core_.Process(cm_->getTimelineUnits(), daisy::System::GetUs());

        while(!input_fifo_.IsEmpty())
        {
            const LooperInput in = input_fifo_.PopFront();
            switch(in.type)
            {
                case LooperInput::Type::NOTE_ON:
                    core_.NoteOn(in.engine, in.key, in.nn, in.note, false);
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
                                                        e.key,
                                                        127.f,
                                                        KeyRequest::Source::SEQUENCER));
        if(!(e.flags & looper::kFlagFromMidi))
            hw_->queueMidiNote(midi_ch_[eng], e.note, 127, NoteOn);
    }

    void LoopNoteOff(uint8_t engine, int8_t key, uint8_t note, bool from_midi) override
    {
        const uint8_t eng = engine > 1 ? 1 : engine;
        engines_[eng]->request_fifo.PushBack(KeyRequest(
            KeyRequest::Type::STOP, 0.f, key, 127.f, KeyRequest::Source::SEQUENCER));
        if(!from_midi)
            hw_->queueMidiNote(midi_ch_[eng], note, 127, NoteOff);
    }

  private:
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
