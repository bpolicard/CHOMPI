#pragma once

#include "hardware.h"
#include "SampleEngine.h"
#include "SliceEngine.h"
#include "ArpeggiatorSequencer.h"
#include "temp_led_stuff.h"
#include "clockManager.h"
#include "PresetManager.h"
#include "NoteLooper.h"
#include "FxEngine.h"
#include "OptionsManager.h"

namespace chompi
{
        static const uint8_t cc_map[3][6] = {
            {20, 21, 22, 23, 24, 25},
            {26, 27, 28, 29, 0, 30},
            {31, 0, 0, 0, 0, 0}};

        static const uint8_t key_map[40] = {
            0x01, /**< ENC_1_SW  page */
            0x02, /**< ENC_2_SW page */
            0x03, /**< ENC_3_SW page */
            0x00, /**< ENC_4_SW page */ /** TODO: was CC 16 out */
            0x04, /**< ENC_5_SW page */
            0x15, /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0x31, /**< KEY_16 */
            0x32, /**< KEY_2 */
            0x34, /**< KEY_3 */
            0x35, /**< KEY_4 */
            0x37, /**< KEY_5 */
            0x33, /**< KEY_17 */
            0x36, /**< KEY_18 */
            0x38, /**< KEY_19 */
            0x30, /**< KEY_1 */
            0x39, /**< KEY_6 */
            0x3b, /**< KEY_7 */
            0x3c, /**< KEY_8 */
            0x3e, /**< KEY_9 */
            0x40, /**< KEY_10 */
            0x3a, /**< KEY_20 */
            0x3d, /**< KEY_21 */
            0x3f, /**< KEY_22 */
            0x41, /**< KEY_11 */
            0x43, /**< KEY_12 */
            0x45, /**< KEY_13 */
            0x47, /**< KEY_14 */
            0x48, /**< KEY_15 */
            0x42, /**< KEY_23 */
            0x44, /**< KEY_24 */
            0x46, /**< KEY_25 */
            0x05, /**< ENC_6_SW page */
            0x17, /**< KEY_27 play cc */
            0x18, /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

        static const uint8_t led_map[40]{
            2,    /**< ENC_1_SW  page */
            3,    /**< ENC_2_SW page */
            4,    /**< ENC_3_SW page */
            1,    /**< ENC_4_SW cc */
            0,    /**< ENC_5_SW page */
            0,    /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0,    /**< KEY_16 */
            23,   /**< KEY_2 */
            22,   /**< KEY_3 */
            21,   /**< KEY_4 */
            20,   /**< KEY_5 */
            1,    /**< KEY_17 */
            2,    /**< KEY_18 */
            3,    /**< KEY_19 */
            24,   /**< KEY_1 */
            19,   /**< KEY_6 */
            18,   /**< KEY_7 */
            17,   /**< KEY_8 */
            16,   /**< KEY_9 */
            15,   /**< KEY_10 */
            4,    /**< KEY_20 */
            5,    /**< KEY_21 */
            6,    /**< KEY_22 */
            14,   /**< KEY_11 */
            13,   /**< KEY_12 */
            12,   /**< KEY_13 */
            11,   /**< KEY_14 */
            10,   /**< KEY_15 */
            7,    /**< KEY_23 */
            8,    /**< KEY_24 */
            9,    /**< KEY_25 */
            9,    /**< ENC_6_SW page */
            7,    /**< KEY_27 play cc */
            8,    /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

    // .00787 ~= what midi was. 1 / 127
    static const float kEncoderFineStep = .003f;
    static const float kEncoderTempoStep = .003125f;
    static const float kEncoderGateStep = .1f;
    static const float kEncoderCoarseStep = .01f;
    static const float kEncoderWtStep = 1.f / 6.f;
    static const float kEncoderCycleStep = 1.f / 33.f;
    static const float kRecDim = .7f;

    uint8_t led_pth_cache[kNumPthLeds][3]; /**< RGB data */
    uint8_t led_smt_cache[kNumSmtLeds][3]; /**< RGB data */

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float orange[3] = {1.f, .6f, .24f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float med_green[3] = {.14f, 1.f, .5f};
    static const float teal[3] = {.14f, 1.f, .92f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float purple[3] = {.58f, .05f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};

    static const uint8_t knob_num_pages[6] = {3, 2, 2, 2, 1, 2};

    /** U5: one colour per quantize grid, used for notes (both views), the left
     *  tempo light, and the grid display. Red is reserved for the playhead.
     *  1/4   1/4T  1/8   1/8T    1/16   1/16T  1/32    1/32T   off */
    static const float *kGridColor[looper::kNumGrids]
        = {green, teal, blue, purple, white, pink, orange, yellow, white};
    static const float kGridLevel[looper::kNumGrids] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, .3f};

    /** U5: colour of a grid, scaled by level, into r g b. Out-of-range grid = off. */
    static void GridColor(int grid, float level, float &r, float &g, float &b)
    {
        if (grid < 0 || grid >= looper::kNumGrids) {
            r = g = b = 0.f;
            return;
        }
        const float lv = level * kGridLevel[grid];
        r = kGridColor[grid][0] * lv;
        g = kGridColor[grid][1] * lv;
        b = kGridColor[grid][2] * lv;
    }

    /** LOOPER: LED brightness for a pulse that peaks on each line of a grid and fades
     *  until the next one. phase: units since the last line; period: units per line;
     *  floor: brightness between pulses. */
    static float LooperPulse(int32_t phase, int32_t period, float floor)
    {
        const float x = 1.f - static_cast<float>(phase) / static_cast<float>(period);
        return floor + (1.f - floor) * x * x;
    }

    /** LOOPER: pitch-knob LED color while the Chroma keys are transposed:
     *  up = yellow toward red, down = blue toward purple, further = further along. */
    static void TransposeLedColor(int semis, float &r, float &g, float &b)
    {
        const float t = static_cast<float>(semis < 0 ? -semis : semis) / NoteLooper::kMaxTranspose;
        const float *from = semis > 0 ? yellow : med_blue;
        const float *to = semis > 0 ? red : purple;
        r = color_xfade(from[0], to[0], t);
        g = color_xfade(from[1], to[1], t);
        b = color_xfade(from[2], to[2], t);
    }

    /** LOOPER: for a moment after the quantize grid changes, show it on white keys 1-9,
     *  each in its grid colour. Bright = grid for new notes; medium = the grid you
     *  scrolled to, when you've hopped away from it with shift; dim = the rest.
     *  While the transport runs, each key pulses at its own subdivision's rate.
     *  Returns false and draws nothing when there's nothing to show. */
    static bool DrawLooperGrid(const NoteLooper *lp)
    {
        if (!lp || !lp->ShowGrid()) {
            return false;
        }
        static const Hardware::SwId grid_keys[looper::kNumGrids] = {
            Hardware::SwId::KEY_1, Hardware::SwId::KEY_2, Hardware::SwId::KEY_3,
            Hardware::SwId::KEY_4, Hardware::SwId::KEY_5, Hardware::SwId::KEY_6,
            Hardware::SwId::KEY_7, Hardware::SwId::KEY_8, Hardware::SwId::KEY_9};

        // all 25 keys off first
        for (size_t i = 7; i < 25 + 7; i++) {
            SetSmtLed(led_map[i], 0, 0, 0);
        }

        const bool running = lp->IsRunning();
        const int32_t bar = lp->BarPhaseUnits();
        const int active = lp->ActiveGridIndex();
        const int home = lp->HomeGridIndex();
        for (int g = 0; g < looper::kNumGrids; g++) {
            float level = g == active ? 1.f : (g == home ? .4f : .15f);
            const int32_t units = looper::kGridUnits[g];
            if (running && units > 0) {
                level *= LooperPulse(bar % units, units, .25f);
            }
            float r, gg, b;
            GridColor(g, level, r, gg, b); // U5
            SetSmtLedFloat(led_map[static_cast<int>(grid_keys[g])], r, gg, b);
        }
        return true;
    }

    /** U5: the step view. The 15 white keys are a window of 15 steps: a note lights
     *  its key in the colour of the grid it was quantized to, the playhead is red
     *  (and wins), everything else is dark. The end black keys show what pressing
     *  them does: green = reveal the rest of this bar, purple = next / previous bar,
     *  red = append dormant bars. The middle black keys are dark (reserved).
     *  Returns false and draws nothing when the step view is off. */
    static bool DrawLooperSteps(const NoteLooper *lp)
    {
        if (!lp || !lp->InStepView()) {
            return false;
        }
        for (size_t i = 7; i < 25 + 7; i++) {
            SetSmtLed(led_map[i], 0, 0, 0);
        }
        const int play = lp->PlayStep();
        for (int i = 0; i < NoteLooper::kViewSteps; i++) {
            const int led = led_map[static_cast<int>(looper_ui::kWhiteKeys[i])];
            if (i == play) {
                SetSmtLedFloat(led, red[0], red[1], red[2]);
                continue;
            }
            const int grid = lp->StepGridAt(i);
            if (grid >= 0) {
                float r, g, b;
                GridColor(grid, 1.f, r, g, b);
                SetSmtLedFloat(led, r, g, b);
            }
        }
        // the end black keys: what a press would do
        const NoteLooper::Nav navs[2] = {lp->NavBack(), lp->NavForward()};
        const int nav_keys[2] = {looper_ui::kBlackFirst, looper_ui::kBlackLast};
        for (int k = 0; k < 2; k++) {
            const float *c = nullptr;
            switch (navs[k]) {
                case NoteLooper::Nav::WITHIN: c = green; break;
                case NoteLooper::Nav::PAGE: c = purple; break;
                case NoteLooper::Nav::ADD: c = red; break;
                default: break;
            }
            if (c) {
                SetSmtLedFloat(led_map[nav_keys[k]], c[0], c[1], c[2]);
            }
        }
        return true;
    }

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(fxEngine *fx, clockManager *clock_manager, ArpeggiatorSequencer *arpSeq, BaseEngine **engines, 
                    Hardware *hw, float** enc_arr, const float** def_arr, uint8_t* page, PresetManager* pre, 
                    OptionsManager *options)
        {
            hw_ = hw;
            engines_ = engines;
            fx_ = fx;
            arpSeq_ = arpSeq;
            clock_manager_ = clock_manager;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel[0] = options->midi_ch_out_chroma;
            midi_channel[1] = options->midi_ch_out_slice;

            // LOOPER: the plain pitch knob is always stepped: in Chroma it transposes
            // the keys, in Slice it steps the sample's pitch. There is no continuous
            // pitch any more, whatever "Pitch Quantize In Shift Menu" says.
            quantized_pitch_ = true;
            record_latch_ = options->record_latch;

            for (int knob = 0; knob < 6; knob++)
            {
                for (int page = 0; page < 3; page++)
                {
                    enc_values[page][knob] = enc_defaults[page][knob];
                }
            }


            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            // 8mm leds
            SetPthLedFloat(0, 0.f, 0.f, 0.f);
            SetPthLedFloat(1, green[0], green[1], green[2]);
            SetPthLedFloat(2, 0.f, 0.f, 0.f);
            SetPthLedFloat(3, 0.f, 0.f, 0.f);
            SetPthLedFloat(4, 0.f, 0.f, 0.f);
            SetPthLedFloat(9, 0.f, 0.f, 0.f);

            // 5mm leds
            SetPthLedFloat(5, 1.f, 1.f, 1.f);
            SetPthLedFloat(6, 1.f, 1.f, 1.f);
            SetPthLedFloat(7, 1.f, 1.f, 1.f);
            SetPthLedFloat(8, 1.f, 1.f, 1.f);

            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);

            init_time = System::GetNow();
        }

        uint32_t last_arm_blink;
        bool arm_blink = true;

        void SetLooper(NoteLooper *looper) { looper_ = looper; }

        /** LOOPER: build signature. For 3 s after the boot rainbow, white keys 1-4 light
         *  green (update-5), so you can tell at a glance which build is on the card. */
        void ShowBuildSignature() { signature_until_ = System::GetNow() + 3000; }

        void ResetSmtLeds()
        {
            for(size_t i = 0; i < 25; i++)
            {
                SetSmtLed(i, 0.f, 0.f, 0.f);
            }
        }

        void CacheLeds()
        {
            std::copy(&led_pth_data[0][0], &led_pth_data[0][0] + kNumPthLeds * 3, &led_pth_cache[0][0]);
            std::copy(&led_smt_data[0][0], &led_smt_data[0][0] + kNumSmtLeds * 3, &led_smt_cache[0][0]);
        }

        void RefreshLeds()
        {
            std::copy(&led_pth_cache[0][0], &led_pth_cache[0][0] + kNumPthLeds * 3, &led_pth_data[0][0]);
            std::copy(&led_smt_cache[0][0], &led_smt_cache[0][0] + kNumSmtLeds * 3, &led_smt_data[0][0]);
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            if (now - freeze_hold > 750 && freeze_pressed) {
                knob_page[3]++;
                knob_page[3] %= knob_num_pages[3];
                freeze_pressed = false;
            }

            // LOOPER: the step view shows the engine that's selected
            if (looper_) {
                looper_->SetViewEngine(fx_->getEngine());
            }

            for(size_t i = 7; i < (25 + 7); i++)
            {
                if (arpSeq_->isKeyPlaying(i) || (arpSeq_->isRestPlaying(i) && arpSeq_->getPlay(1) && fx_->getEngine() == SLICE)) {
                    // U5: a looped note lights its key in its grid's colour; a key you're
                    // playing is white
                    const int grid = looper_ ? looper_->KeyGrid(i) : -1;
                    if (grid >= 0) {
                        float r, g, b;
                        GridColor(grid, 1.f, r, g, b);
                        SetSmtLedFloat(led_map[i], r, g, b);
                    }
                    else {
                        SetSmtLedFloat(led_map[i], 1.f, 1.f, 1.f);
                    }
                }
                else if (arpSeq_->getLatch() && arpSeq_->isKeyInSeq(i, fx_->getEngine())) {
                    if (i == 28 && fx_->getEngine() == SLICE) {
                        float ledcolors[3];
                        arpSeq_->getColors(ledcolors);
                        SetSmtLedFloat(led_map[i], ledcolors[0], ledcolors[1], ledcolors[2]);
                    }
                    else {
                        if (fx_->getEngine() == CHROMATIC) {
                            SetSmtLedFloat(led_map[i], 1.f, 0.f, 0.f);
                        }
                        else {
                            SetSmtLedFloat(led_map[i], yellow[0], yellow[1], yellow[2]);
                        }
                    }
                }
                else {
                    SetSmtLed(led_map[i], 0, 0, 0);
                }
            }

            // U5: Slice key mode: the end black keys slide the 15 keys over the 16
            // slices; the one that still has somewhere to go lights dim purple
            if (fx_->getEngine() == SLICE && !(looper_ && looper_->InStepView())) {
                const int off = SliceOffset();
                const float lv = .35f;
                if (off == 0) {
                    SetSmtLedFloat(led_map[looper_ui::kBlackLast], purple[0] * lv, purple[1] * lv, purple[2] * lv);
                }
                else {
                    SetSmtLedFloat(led_map[looper_ui::kBlackFirst], purple[0] * lv, purple[1] * lv, purple[2] * lv);
                }
            }

            // LOOPER: step view (replaces the key display while it's on)
            DrawLooperSteps(looper_);

            // =========   encoders   =========
            for (int i = 0; i < 6; i++)
            {
                uint8_t page = knob_page[i];
                float value = enc_values[page][i];

                float r = 0.f; 
                float g = 0.f;
                float b = 0.f;
                switch (i)
                {
                case 0: // speed, gain, pan
                {
                    if(page == 0) // pitch
                    {
                        float idx = value < .5f ? value * 2.f : (1.f - value) * 2.f; // 0 - 1 - 0
                        r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                        g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                        b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);
                        // LOOPER: in Chroma the plain knob transposes the keys; show it
                        if (fx_->getEngine() == CHROMATIC && looper_ && looper_->Transpose() != 0) {
                            TransposeLedColor(looper_->Transpose(), r, g, b);
                        }
                    }
                    else if(page == 1) // sample volume
                    {
                        r = color_triple_xfade(blue[0], pink[0], red[0], value);
                        g = color_triple_xfade(blue[1], pink[1], red[1], value);
                        b = color_triple_xfade(blue[2], pink[2], red[2], value);
                    }
                    else { // filter
                        r = color_xfade(purple[0], white[0], value);
                        g = color_xfade(purple[1], white[1], value);
                        b = color_xfade(purple[2], white[2], value);
                    }

                    SetPthLedFloat(1, r, g, b);
                }
                break;
                case 1:
                {
                    if (page == 0) // sample start
                    {
                        r = color_xfade(yellow[0], orange[0], value);
                        g = color_xfade(yellow[1], orange[1], value);
                        b = color_xfade(yellow[2], orange[2], value);
                    }
                    else // attack
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);
                        
                    }

                    SetPthLedFloat(2, r, g, b);
                    break;
                }
                case 2:
                {
                    if (page == 0) // sample end
                    {
                        r = color_xfade(orange[0], red[0], value);
                        g = color_xfade(orange[1], red[1], value);
                        b = color_xfade(orange[2], red[2], value);
                    }
                    else // release
                    {
                        r = color_xfade(purple[0] * .2f, purple[0], value);
                        g = color_xfade(purple[1] * .2f, purple[1], value);
                        b = color_xfade(purple[2] * .2f, purple[2], value);
                    }

                    SetPthLedFloat(3, r, g, b);
                    break;
                }
                case 3: // magic
                {
                    if (page == 0) // delay main
                    {
                        float colors[3];
                        fx_->getColors(colors);
                        r = colors[0];
                        g = colors[1];
                        b = colors[2];
                    }
                    else if (page == 1) // delay mix
                    {
                        r = color_triple_xfade(yellow[0], orange[0], red[0], value);
                        g = color_triple_xfade(yellow[1], orange[1], red[1], value);
                        b = color_triple_xfade(yellow[2], orange[2], red[2], value);
                    }

                    SetPthLedFloat(4, r, g, b);

                    break;
                }
                case 4: // transport
                {
                    if (transport_held) {
                        float leds_[6];
                        clock_manager_->fillLEDdata(leds_, fx_->getEngine());
                        
                        SetPthLedFloat(5, leds_[0], leds_[1], leds_[2]);
                        SetPthLedFloat(6, leds_[3], leds_[4], leds_[5]);
                    }
                    else {
                        // U5: the right light is the beat and is always green; the left
                        // light is the quantize grid, in the grid's colour (green at
                        // 1/4, when it pulses at the same rate). Running: both pulse.
                        // Stopped: both dim. The tempo gradient is gone.
                        float leds_[6]; // left LED, then right LED
                        const int grid = looper_ ? looper_->ActiveGridIndex() : looper::kGridSixteenth;
                        GridColor(grid, 1.f, leds_[0], leds_[1], leds_[2]);
                        leds_[3] = green[0];
                        leds_[4] = green[1];
                        leds_[5] = green[2];

                        float beat_lvl = .25f;
                        float sub_lvl = .25f;
                        if (looper_ && looper_->IsRunning()) {
                            const int32_t bar = looper_->BarPhaseUnits();
                            const int32_t sub = looper::kGridUnits[grid];
                            beat_lvl = LooperPulse(bar % looper::kUnitsPerBeat, looper::kUnitsPerBeat, .08f);
                            sub_lvl = sub > 0 ? LooperPulse(bar % sub, sub, .08f) : .3f;
                        }
                        for (int c = 0; c < 3; c++) {
                            leds_[c] *= sub_lvl;
                            leds_[3 + c] *= beat_lvl;
                        }

                        SetPthLedFloat(5, leds_[0], leds_[1], leds_[2]);
                        SetPthLedFloat(6, leds_[3], leds_[4], leds_[5]);

                        if (arpSeq_->getPlay(fx_->getEngine())) {
                            if (arpSeq_->getLeftLights()) {
                                SetPthLedFloat(5, 0.f, 0.f, 0.f);
                            }
                            else {
                                SetPthLedFloat(6, 0.f, 0.f, 0.f);
                            }
                        }
                    }

                    break;
                }
                case 5: // gain
                {
                    if(batt_display && System::GetNow() - batt_hold > 2000)
                    {
                        const float* color = &green[0];

                        switch(hw_->GetBatteryLevel())
                        {
                            case Hardware::BatteryLevel::FULL:
                                color = &white[0];
                            break;
                            case Hardware::BatteryLevel::HIGH:
                                color = &green[0];
                            break;
                            case Hardware::BatteryLevel::MEDIUM:
                                color = &yellow[0];
                            break;
                            case Hardware::BatteryLevel::LOW:
                                color = &red[0];
                            break;
                            default:
                            break;
                        }

                        r = color[0];
                        g = color[1];
                        b = color[2];
                    }
                    else if (page == 0)
                    {
                        float vu_sample = fx_->getVUSample();
                        r = value * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                        g = value * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                        b = value * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
                    }
                    else
                    {
                        r = color_xfade(blue[0], red[0], value);
                        g = color_xfade(blue[1], red[1], value);
                        b = color_xfade(blue[2], red[2], value);
                    }
                    SetPthLedFloat(9, r, g, b);
                    break;
                }
                default:
                    break;
                }
            }

            /** PTH leds */
            float r, g, b;
            // play key

            r = g = b = 0.f;

            // LOOPER: teal while the loop / clock is running; white in the step view
            // (dim white when stopped in the step view)
            if (looper_ && looper_->InStepView()) {
                r = g = b = looper_->IsRunning() ? 1.f : .25f;
            }
            else if (looper_ && looper_->IsRunning()) {
                r = teal[0];
                g = teal[1];
                b = teal[2];
            }
            else {
                r = g = b = 0.f;
            }

            SetPthLedFloat(led_map[33], r, g, b);

            // loop key

            // LOOPER state:
            //   armed: blinking red   recording: red   overdub: pink
            //   playing: green (dim while stopped)   memory full: blinking yellow
            r = g = b = 0.f;
            if (looper_) {
                const bool blink_on = (now / 250) % 2 == 0;
                const float *c = nullptr;
                float level = 1.f;
                switch (looper_->GetState()) {
                    case looper::State::ARMED:
                        if (blink_on) c = red;
                        break;
                    case looper::State::RECORDING: c = red; break;
                    case looper::State::OVERDUB: c = pink; break;
                    case looper::State::PLAYING:
                        c = green;
                        level = looper_->IsRunning() ? 1.f : .25f;
                        break;
                    default: break;
                }
                if (looper_->IsFull() && blink_on) {
                    c = yellow;
                    level = 1.f;
                }
                if (c) {
                    r = c[0] * level;
                    g = c[1] * level;
                    b = c[2] * level;
                }
            }

            SetPthLedFloat(led_map[34], r, g, b);

            // chompi key

            fx_->SetInputMonitor(!switch_state);
            if (!switch_state) {
                if (fx_->getRecording()) {
                    r = red[0];
                    g = red[1];
                    b = red[2];
                }
                else {
                    float vu_sample = fx_->getVUSampleInput();

                    r = color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                    g = color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                    b = color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
                }

            }
            else {
                if (chompi_key_pressed) {
                    r = .67f;
                    g = 0.f;
                    b = 1.f;
                    }
                else {
                    r = g = b = 0.f;
                }
            }

            SetPthLedFloat(led_map[5], r, g, b);

            // LOOPER: quantize grid display (overrides the keys for a moment)
            DrawLooperGrid(looper_);

            // LOOPER: build signature (see ShowBuildSignature): white keys 1-4 green
            if (signature_until_ != 0 && now < signature_until_) {
                static const Hardware::SwId sig_keys[4] = {
                    Hardware::SwId::KEY_1, Hardware::SwId::KEY_2, Hardware::SwId::KEY_3, Hardware::SwId::KEY_4};
                for (int k = 0; k < 4; k++) {
                    SetSmtLedFloat(led_map[static_cast<int>(sig_keys[k])], green[0], green[1], green[2]);
                }
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
                return false;

            bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            // NO CONNECT, SKIP THESE
            case static_cast<uint16_t>(Hardware::SwId::NC_1): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_2): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_3): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_4): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_5): // fall through
                                                              // case static_cast<uint16_t>(Hardware::SwId::NC_6): // caught in ui.h
                break;

            // encoder clicks, toggle pages
            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // fall through
            {
                if (buttonID == 2) {
                    freeze_pressed = rising;
                    if (rising) {
                        freeze_hold = System::GetNow();
                    }
                    else if (!rising && System::GetNow() - freeze_hold < 750) {
                        if (knob_page[3] == 0) {
                            fx_->toggleGranularFreeze();
                        }
                        else {
                            knob_page[3] = 0;
                        }
                    }
                }
                else if(!rising)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && System::GetNow() - batt_hold < 2000)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }

                batt_hold = System::GetNow();
                batt_display = rising;

                break;
            }

            // reset the looper pitch
            case ENC_5_SW:
            {
                if (!rising)
                {
                    hw_->queueMidiCC(midi_channel[fx_->getEngine()], cc_map[0][4], enc_values[0][4] * 127.f);
                    transport_held = false;
                }
                else {
                    enc_values[0][4] = clock_manager_->processTapClock(enc_values[0][4]);
                    transport_held = true;
                }

                break;
            }

            // toggle. We're not using this anymore, just here in case something breaks
            case static_cast<uint16_t>(Hardware::SwId::SW_TOG): // fall through
                break;

            // CC buttons
            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // play
            {
                // LOOPER: Play starts / stops the loop and the clock on its release,
                // and Play + Loop held for a second toggles the step view. Both are
                // handled by NoteLooper (fed from ui.h), so nothing happens here.
                last_arm_blink = System::GetNow();

                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // loop
            {
                // LOOPER: the arp latch is gone. Loop key gestures are handled by
                // NoteLooper (fed from ui.h), so nothing else happens here.
                last_arm_blink = System::GetNow();

                hw_->queueMidiCC(midi_channel[fx_->getEngine()], 15, rising ? 127 : 0);
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
            {
                chompi_key_pressed = rising;
                if (!switch_state)
                {
                    hw_->queueMidiCC(midi_channel[fx_->getEngine()], 14, rising ? 127 : 0);
                    if (!fx_->getRecording() && rising) {
                        fx_->StartNewRecording();
                    }
                    else if (fx_->getRecording() && ((!rising && !record_latch_) || (rising && record_latch_))) {
                        fx_->StopRecording();
                        engines_[fx_->getEngine()]->setSample(14);
                        if (fx_->getEngine() == CHROMATIC && engines_[SLICE]->getVoiceSlot() == 15) {
                            engines_[SLICE]->setSample(14);
                        }

                        enc_values[0][0] = enc_defaults[0][0];
                        enc_values[1][0] = enc_defaults[1][0];
                        enc_values[0][1] = enc_defaults[0][1];
                        enc_values[0][2] = enc_defaults[0][2];
                        enc_values[2][0] = enc_defaults[2][0];

                        for (size_t i = 0; i < kNumEngines; ++i) {
                            engines_[fx_->getEngine()]->resetGlobalPitchQuant();
                            engines_[fx_->getEngine()]->setGlobalPitchFree(enc_values[0][0]);
                            engines_[fx_->getEngine()]->setSampleVolume(enc_values[1][0]);
                            engines_[fx_->getEngine()]->setStartPoint(enc_values[0][1]);
                            engines_[fx_->getEngine()]->setEndPoint(enc_values[0][2]);
                            engines_[fx_->getEngine()]->setMasterCutoff(enc_values[2][0]);
                        }
                        record_reset = true;

                    }
                }
 
                break;
            }

            // keys
            default:
            {
                const bool is_nav_key = buttonID == looper_ui::kBlackFirst || buttonID == looper_ui::kBlackLast;

                // U5: in the step view the keys edit the loop instead of playing:
                // white keys are steps, the end black keys slide the window, the
                // middle black keys are reserved. A key that was already sounding
                // when the view opened is still released below.
                if (looper_ && looper_->InStepView()) {
                    if (rising) {
                        if (!isRetriggering) {
                            if (is_nav_key) {
                                looper_->QueueStepNav(buttonID == looper_ui::kBlackLast ? 1 : -1);
                            }
                            else if (!looper_ui::IsBlackKey(buttonID)) {
                                StepPress(buttonID);
                            }
                        }
                        break;
                    }
                    if (buttonID >= kNumKeyIds || !looper_owns_key_[buttonID]) {
                        break; // this press never started a note
                    }
                }
                // U5: Slice key mode: the end black keys slide the keyboard over the
                // 16 slices instead of playing (they never played anything in Slice)
                else if (fx_->getEngine() == SLICE && is_nav_key) {
                    if (rising && !isRetriggering) {
                        SetSliceOffset(buttonID == looper_ui::kBlackLast ? 1 : 0);
                    }
                    break;
                }

                // LOOPER: in Chroma the pitch knob transposes the keys (NoteLooper):
                // a key plays, records and sends its note plus the transpose.
                // U5: in Slice, nn names the slice (slice + 1) so a recorded note keeps
                // its slice whatever the slot window does later.
                const int transpose = (fx_->getEngine() == CHROMATIC && looper_) ? looper_->Transpose() : 0;
                const float transpose_nn = fx_->getEngine() == CHROMATIC
                                               ? static_cast<float>(key_map[buttonID] - 60 + transpose)
                                               : SliceNn(buttonID);
                const uint8_t midi_note = static_cast<uint8_t>(key_map[buttonID] + transpose);
                if (rising)
                {
                    // real keypress
                    if(!isRetriggering)
                    {
                        // LOOPER: a plain key press (no arpeggiator) is sounded by the
                        // looper: right away, or with live quantize on the next grid
                        // line, MIDI out included. It also stops it when you let go.
                        // Remember which keys it took, so the release goes to it too.
                        const bool via_looper = looper_ && looper_->SoundsLiveNotes()
                                                && !arpSeq_->getPlay(fx_->getEngine())
                                                && !arpSeq_->getLatch()
                                                && !arpSeq_->getSustain();
                        if (buttonID < kNumKeyIds) {
                            looper_owns_key_[buttonID] = via_looper;
                        }

                        if (arpSeq_->getPlay(fx_->getEngine())) {
                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                static_cast<float>(key_map[buttonID] - 60), buttonID, 127.f));
                        }
                        else if (arpSeq_->getLatch()) {
                            if (!(arpSeq_->getSustain() && arpSeq_->isKeyInSeq(buttonID, fx_->getEngine()))) {
                                engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                    transpose_nn, buttonID, 127.f));
                            }

                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                static_cast<float>(key_map[buttonID] - 60), buttonID, 127.f));

                        }
                        else if (!via_looper) {
                            engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                transpose_nn, buttonID, 127.f));
                        }
                        
                        if (!arpSeq_->getPlay(fx_->getEngine()) && !via_looper) {
                            hw_->queueMidiNote(midi_channel[fx_->getEngine()], midi_note, 127, NoteOn);
                        }

                        // LOOPER: record the key press (and sound it, if it's the looper's)
                        if (looper_) {
                            looper_->QueueNoteOn(fx_->getEngine(), buttonID, transpose_nn, midi_note,
                                                 via_looper);
                        }
                    }
                }
                else
                {
                    if(!isRetriggering)
                    {
                        // LOOPER: if the looper is sounding this key, it stops it too
                        bool via_looper = false;
                        if (buttonID < kNumKeyIds) {
                            via_looper = looper_owns_key_[buttonID];
                            looper_owns_key_[buttonID] = false;
                        }

                        //regular key mode. Stop transpose_nn_ is 0 because you don't need it? Seems a bit weird
                        if (!via_looper && !arpSeq_->getSustain() && !arpSeq_->checkNotePlaying(key_map[buttonID] - 60)) {
                            engines_[fx_->getEngine()]->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                                transpose_nn, buttonID, 127.f));
                        }
                        if (arpSeq_->getPlay(fx_->getEngine()) || (arpSeq_->getLatch() && !arpSeq_->getSustain())) {
                            arpSeq_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                                static_cast<float>(key_map[buttonID] - 60), buttonID, 127.f));
                        }
                        if (!via_looper && !arpSeq_->getPlay(fx_->getEngine()) && !arpSeq_->getSustain()) {
                            hw_->queueMidiNote(midi_channel[fx_->getEngine()], midi_note, 127, NoteOff);
                        }

                        // LOOPER: record the key release (sets the note's length, and
                        // stops the note if the looper is sounding it)
                        if (looper_) {
                            looper_->QueueNoteOff(buttonID);
                        }
                    }
                }
                break;
            }
            }

            return true;
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            uint8_t page = knob_page[encoderID];

            // overrode this to mean increment vs force knob position (used for CCs)
            if(stepsPerRevolution > 0)
            {
                enc_values[page][encoderID] = turns / 127.f;
                if (encoderID == 4) {
                    //Set tempo
                    size_t tempo = 160 + enc_values[0][4] * 320;
                    clock_manager_->setTempo(tempo);
                    return true;
                }
            }
            else{
                float inc = turns * kEncoderCoarseStep;

                // fine steps for pitch, sample start, and sample end
                if ((encoderID == 0 && page == 0 && !quantized_pitch_) || (encoderID == 3 && page == 0))
                {
                    inc = turns * kEncoderFineStep;
                }
                else if (encoderID == 4 && page == 0) {
                    inc = turns * kEncoderTempoStep;
                }
                else if (encoderID == 0 && page == 1) {
                    inc = turns * kEncoderCycleStep;
                }
                else if ((encoderID == 1 && page == 0) || (encoderID == 2 && page == 0)) {
                    if (fx_->getEngine() == CHROMATIC) {
                        float range = enc_values[0][2] - enc_values[0][1];
                        float step;
                        if (range > .05f) {
                            step = kEncoderFineStep; // mild curve
                        }
                        else {
                            float t = range / 0.05f; // 1 at .05, 0 at 0
                            float curved = powf(t, 3.5f); // sharper than 2.5
                            step = curved * kEncoderCoarseStep;
                        }
                        step = fclamp(step, 0.0001f, 1.f);
                        inc = turns * step;
                    }
                    else {
                        inc = turns * kEncoderFineStep;
                    }
                }
                else if (encoderID == 0 && page == 0 && quantized_pitch_) {
                    inc = 0.f;
                }

                enc_values[page][encoderID] += inc;
            }

            // clip
            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);

            switch (encoderID) {
                case 0: {
                    if (page == 0) {
                        // LOOPER: the plain pitch knob. Chroma: transposes the keys in
                        // fifths and fourths (the loop keeps its pitch). Slice: steps the
                        // sample's pitch (TEMPO's stepped pitch). A MIDI CC still sets the
                        // sample's pitch continuously (external automation).
                        if (stepsPerRevolution > 0 || !quantized_pitch_) {
                            engines_[fx_->getEngine()]->setGlobalPitchFree(enc_values[0][0]);
                        }
                        else if (fx_->getEngine() == CHROMATIC && looper_) {
                            looper_->StepTranspose(turns);
                        }
                        else {
                            enc_values[0][0] = engines_[fx_->getEngine()]->setGlobalPitchQuantized(turns, enc_values[0][0]);
                        }
                    }
                    else if (page == 1) {
                        float smooth_volume = powf(enc_values[1][0], 2.0f);
                        engines_[fx_->getEngine()]->setSampleVolume(smooth_volume);
                    }
                    else {
                        engines_[fx_->getEngine()]->setMasterCutoff(enc_values[2][0]);
                    }
                }
                break;
                case 1: {
                    if (page == 0) {
                        enc_values[0][1] = fclamp(enc_values[0][1], 0, enc_values[0][2] - kEncoderFineStep);
                        engines_[fx_->getEngine()]->setStartPoint(enc_values[0][1]);
                    }
                    else {
                        engines_[fx_->getEngine()]->setAttack(enc_values[1][1]);
                    }
                }
                break;
                case 2: {
                    if (page == 0) {
                        enc_values[0][2] = fclamp(enc_values[0][2], enc_values[0][1] + kEncoderFineStep, 1.f);
                        engines_[fx_->getEngine()]->setEndPoint(enc_values[0][2]);
                    }
                    else {
                        engines_[fx_->getEngine()]->setRelease(enc_values[1][2]);
                    }
                }
                break;
                case 3: {
                    if (page == 0) {
                        fx_->setGranularMain(enc_values[0][3]);
                    }
                    else {
                        fx_->setGranularMix(enc_values[1][3], fx_->getEngine());
                    }
                }
                break;
                case 4: {
                    if (transport_held && looper_) {
                        // LOOPER: press + turn the tempo knob scrolls the quantize grid
                        looper_->ScrollGrid(turns);
                    }
                    else if (clock_manager_->getClockMode() == FREE) {
                        clock_manager_->changeTempo(turns);
                    }
                    else {
                        clock_manager_->changeDiv(turns, fx_->getEngine());
                    }
                }
                break;
                case 5: {
                    if (page == 0) {
                        fx_->setGain(enc_values[0][5]);
                    }
                    else {
                        fx_->setInputGain(enc_values[1][5]);
                    }
                }
                break;
            }

            if (stepsPerRevolution == 0) {
                hw_->queueMidiCC(midi_channel[fx_->getEngine()], cc_map[page][encoderID], enc_values[page][encoderID] * 127);
            }

            return true;
        }

        bool getRecordReset() {
            return record_reset;
        }

        void resetRecordReset() {
            record_reset = false;
        }

        void SetSwitchState(bool state)
        {
            switch_state = state; 
        }

        bool getSwitchState() {
            return switch_state;
        }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        /** U5: the Slice engine (engines_[SLICE] is a sliceEngine, see chompi_main) */
        sliceEngine *Slice() { return static_cast<sliceEngine *>(engines_[SLICE]); }
        int SliceOffset() { return Slice()->getSliceOffset(); }
        void SetSliceOffset(int off)
        {
            Slice()->setSliceOffset(off);
            if (looper_) {
                looper_->SetSliceOffset(off); // so looped notes light the keys that play them now
            }
        }

        /** U5: the nn a Slice key sends: its slice + 1 at the current window, or 0 for
         *  a key that plays no slice (so the engine ignores it as before). */
        float SliceNn(int key)
        {
            if (key < 0 || key > 31 || sliceMap[key] == 99) {
                return 0.f;
            }
            return static_cast<float>(static_cast<int>(sliceMap[key]) + SliceOffset() + 1);
        }

        /** LOOPER: a white key pressed in the step view: place the selected note(s) at
         *  that step, or clear the step if it already has notes (NoteLooper decides).
         *  With no selection, a random white key's note is placed. Chroma notes take
         *  the current transpose, as a key press would; Slice notes take their slice
         *  at the current window. */
        void StepPress(uint16_t buttonID)
        {
            const int step = looper_->StepIndexOfKey(buttonID);
            if (step < 0) {
                return; // not a step key
            }
            const uint8_t eng = fx_->getEngine();
            const int transpose = eng == CHROMATIC ? looper_->Transpose() : 0;
            uint32_t sel = looper_->SelectedKeys();
            if (eng == SLICE) {
                // only keys that play a slice can be placed
                for (int k = NoteLooper::kFirstKey; k <= NoteLooper::kLastKey; k++) {
                    if (k > 31 || sliceMap[k] == 99) {
                        sel &= ~(1u << k);
                    }
                }
            }
            if (sel == 0) {
                sel = 1u << looper_ui::kWhiteKeys[RandomKeyIndex() % NoteLooper::kViewSteps];
            }
            bool first = true;
            for (int k = NoteLooper::kFirstKey; k <= NoteLooper::kLastKey; k++) {
                if (!((sel >> k) & 1u)) {
                    continue;
                }
                const float nn = eng == CHROMATIC ? static_cast<float>(key_map[k] - 60 + transpose) : SliceNn(k);
                const uint8_t note = static_cast<uint8_t>(key_map[k] + transpose);
                looper_->QueueStepEdit(eng, step, k, nn, note, first);
                first = false;
            }
        }

        /** LOOPER: 0-24, a different one each time (for random step notes) */
        int RandomKeyIndex()
        {
            if (rng_ == 0) {
                rng_ = System::GetUs() | 1u;
            }
            rng_ = rng_ * 1664525u + 1013904223u;
            return static_cast<int>((rng_ >> 8) % 25u);
        }

        Hardware *hw_;
        BaseEngine **engines_;
        ArpeggiatorSequencer *arpSeq_;
        fxEngine *fx_;

        clockManager *clock_manager_;
        PresetManager* presets_;

        float** enc_values;
        const float** enc_defaults;

        /** todo: these really shouldn't be stored in here
         *  Gonna move them out to a midi engine later
        */ 
        uint8_t midi_channel[kNumEngines] = {0, 0};
        bool switch_state = false;
        NoteLooper *looper_ = nullptr;
        // LOOPER: keys whose current press the looper is sounding (it stops them too)
        static constexpr size_t kNumKeyIds = static_cast<size_t>(Hardware::SwId::SR_LAST);
        bool looper_owns_key_[kNumKeyIds] = {};
        uint32_t rng_ = 0;              // LOOPER: random step notes
        uint32_t signature_until_ = 0;  // LOOPER: build signature shown until this time
        bool chompi_key_pressed = false;
        bool transport_held = false;
        uint8_t* knob_page;
        bool quantized_pitch_;
        bool record_latch_;

        bool record_reset = false;

        bool batt_display, freeze_pressed;
        uint32_t batt_hold, freeze_hold;
    };

} // namespace chompi