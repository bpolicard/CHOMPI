#pragma once
#include "EngineBase.h"
#include "daisysp.h"
#include "SampleManager.h"
#include "hardware.h"

struct SliceInfo {
    void *start;
    size_t numSamples;
};

constexpr size_t kMaxSlices = 16;

constexpr size_t sliceMap[32] = {99, 99, 99, 99, 99, 99, 99, 99, 1, 2, 3, 4, 99, 99, 99, 
                                    0, 5, 6, 7, 8, 9, 99, 99, 99, 10, 11, 12, 13, 14, 99, 99, 99};

class sliceVoice {
    public:
    sliceVoice() {}
    ~sliceVoice() {}

    void Init(float sr) {
        player_.Init(sr);
        player_.setFrequency(440.f);
        player_.setLoop(false);
        key = -1;
        slice = -1; // LOOPER
        amp_env_.Init(sr);
        amp_env_.SetSustainLevel(1.f);
        amp_env_.SetAttackTime(.005f);
        amp_env_.SetReleaseTime(.005f);

        sustain = true;
    }

    void setFrequency(float freq) {
        player_.setFrequency(freq);
    }

    void Process(float *sigl, float *sigr) {
        float l = 0.f;
        float r = 0.f;
        if (amp_env_.IsRunning()) {
            player_.PopStereoSamps(&l, &r);
        }
        if (!sustain && amp_env_.GetCurrentSegment() == daisysp::ADSR_SEG_DECAY) {
            gate_ = false;
        }
        *sigl += l * amp_env_.Process(gate_);
        *sigr += r * amp_env_.Process(gate_);
    }

    void setStartEnd(void *st, size_t ns){
        player_.setSample(st, ns);
    }

    samplePlayer player_;
    bool gate_;
    int prioroty;
    float frequency;
    int key;
    int slice; // LOOPER: which of the 16 slices this voice plays (-1 = none yet)
    float nn;
    bool activeFromUser;
    bool activeFromSequencer;
    Adsr amp_env_;
    bool sustain;
    private:
};

class sliceEngine : public BaseEngine {
    public:
    sliceEngine() {}
    ~sliceEngine() {}

    sliceVoice myVoices[NUM_VOICES];

    void Init(sampleManager *sample_manager, float sample_rate) override {
        osc.Init(sample_rate);
        osc.SetAmp(.1f);

        sample_manager_ = sample_manager;

        filter_.Init(sample_rate);
        setMasterCutoff(.5f);
        setMasterRes(0.f);

        rd_l_.Init();
        rd_l_.SetFreq(1.f);
        rd_r_.Init();
        rd_r_.SetFreq(1.f);

        pan_ = pan_target_ = .5f;

        stored_release_ = .005f;

        reduce_ = false;
        slice_offset_ = 0; // LOOPER

        sample_vol_ = sample_vol_target_ = .6f;
        global_pitch_ = 1.f;
        reverse = fifth = false;
        encoder_chunk = 0;

        voice_slot_ = 1;
        loop = false;
        sustain = true;

        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].Init(sample_rate);
        }
    }

    void Process(const float *const *in, float **out, size_t size) override {
        float outl[size];
        float outr[size];
        for (size_t i = 0; i < size; ++i) {
            fonepole(sample_vol_, sample_vol_target_, .001f);
            fonepole(pan_, pan_target_, .001f);
            float sigl = 0.f;
            float sigr = 0.f;
            for (size_t voice = 0; voice < NUM_VOICES; ++voice) {
                myVoices[voice].Process(&sigl, &sigr);
            }
            float pan_l = (1.f - pan_) * 2.f;
            float pan_r = pan_ * 2.f;
            outl[i] = sigl * .125f * sample_vol_ * pan_l;
            outr[i] = sigr * .125f * sample_vol_ * pan_r;
        }

        for (size_t i = 0; i < size; ++i) {
            if (reduce_) {
                outl[i] = rd_l_.Process(outl[i]);
                outr[i] = rd_r_.Process(outr[i]);
            }
            else {
                rd_l_.Process(outl[i]);
                rd_r_.Process(outr[i]);
            }
        }

        filter_.SetControl(master_cutoff_);
        for (size_t i = 0; i < size; ++i) {
            filter_.Process(outl[i], outr[i], &outl[i], &outr[i]);
        }

        for (size_t i = 0; i < size; ++i) {
            out[0][i] = outl[i];
            out[1][i] = outr[i];
        }
    }

    void Prepare() override {
        ProcessKeyReqs();
    }

    // LOOPER: a request is for a slice if its key is a slice key, or if it names
    // its slice outright (transpose_nn = slice + 1). The looper uses key ids above
    // 31 for a looped slice that no key plays at the current slot window.
    static bool isSliceRequest(const KeyRequest &req) {
        if (req.key_ < 0) {
            return false;
        }
        if (req.transpose_nn_ >= 1.f) {
            return true;
        }
        return req.key_ <= 31 && sliceMap[req.key_] != 99;
    }

    void ProcessKeyReqs() {
        while (!request_fifo.IsEmpty())
        {
            KeyRequest req = request_fifo.PopFront();
            
            if (!isSliceRequest(req)) {
                continue;
            }

            if (req.type_ == KeyRequest::Type::START) {
                StartPlayback(req.transpose_nn_, req.key_, req.vel_, req.src_);
            }
            else if (req.type_ == KeyRequest::Type::STOP) {
                StopPlayback(req.key_, req.src_);
            }
        }
    }

    // LOOPER: the 15 slice keys reach 16 slices through a window: key n plays
    // slice n + offset (offset 0 or 1). A request can also name its slice outright
    // (transpose_nn = slice + 1, as a looped note does, so a recorded note keeps
    // playing the slice it was played with whatever the window does now).
    void setSliceOffset(int offset) {
        slice_offset_ = offset < 0 ? 0 : (offset > 1 ? 1 : offset);
    }

    int getSliceOffset() const {
        return slice_offset_;
    }

    int sliceForRequest(float transpose_nn, int key) const {
        int s;
        if (transpose_nn >= 1.f) {
            s = static_cast<int>(transpose_nn) - 1;
        }
        else if (key >= 0 && key <= 31 && sliceMap[key] != 99) {
            s = static_cast<int>(sliceMap[key]) + slice_offset_;
        }
        else {
            s = 0;
        }
        if (s < 0) s = 0;
        if (s >= static_cast<int>(kMaxSlices)) s = kMaxSlices - 1;
        return s;
    }

    void StartPlayback(float transpose_nn, int key, float vel, KeyRequest::Source src) {
        const int slice = sliceForRequest(transpose_nn, key); // LOOPER

        // Find if the key is already being played
        int idx = -1;
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                idx = i;
                break;
            }
        }
        
        // Retrigger a voice already playing
        if (idx != -1) {
            int p;
            myVoices[idx].gate_ = true;
            if (slice != myVoices[idx].slice) { // LOOPER: same key, other slice
                myVoices[idx].setStartEnd(mySlices[slice].start, mySlices[slice].numSamples);
                myVoices[idx].slice = slice;
            }
            myVoices[idx].player_.resetPlayer(myVoices[idx].amp_env_.IsRunning());
            p = myVoices[idx].prioroty;
            myVoices[idx].prioroty = 0;  // Reset priority for this voice
            if (src == KeyRequest::Source::USER) {
                myVoices[idx].activeFromUser = true;
            }
            else {
                myVoices[idx].activeFromSequencer = true;
            }
            // Shift other priorities
            for (int i = 0; i < NUM_VOICES; ++i) {
                if (myVoices[i].prioroty < p) {
                    myVoices[i].prioroty++;
                }
            }
        }
        // If the key isn't playing, take the lowest priority voice with gate off, or the oldest if all gates are on
        else {
            int voiceToSteal = -1;
            int oldestVoice = -1;
            int maxPriority = -1;

            for (int i = 0; i < NUM_VOICES; ++i) {
                // Find the lowest priority voice with the gate off
                if (!myVoices[i].gate_ && myVoices[i].prioroty > maxPriority) {
                    voiceToSteal = i;
                    maxPriority = myVoices[i].prioroty;
                }
                // Track the oldest voice (highest priority) in case all gates are on
                if (myVoices[i].prioroty == NUM_VOICES) {
                    oldestVoice = i;
                }
            }

            // If all voices are playing (gates on), steal the oldest one
            if (voiceToSteal == -1) {
                voiceToSteal = oldestVoice;
            }

            // Assign the new note to the selected voice
            if (voiceToSteal != -1) {
                myVoices[voiceToSteal].setStartEnd(mySlices[slice].start, mySlices[slice].numSamples);
                myVoices[voiceToSteal].slice = slice; // LOOPER
                myVoices[voiceToSteal].setFrequency(keyToFrequency(0.f)); // LOOPER: slices aren't pitched by nn
                myVoices[voiceToSteal].key = key;
                myVoices[voiceToSteal].nn = 0.f;
                myVoices[voiceToSteal].gate_ = true;
                myVoices[voiceToSteal].player_.resetPlayer(myVoices[voiceToSteal].amp_env_.IsRunning());
                myVoices[voiceToSteal].prioroty = 0;  // Reset priority
                if (src == KeyRequest::Source::USER) {
                    myVoices[voiceToSteal].activeFromUser = true;
                }
                else {
                    myVoices[voiceToSteal].activeFromSequencer = true;
                }
            }

            // Increase the priority for all other voices
            for (int i = 0; i < NUM_VOICES; ++i) {
                if (i != voiceToSteal) {
                    myVoices[i].prioroty++;
                }
            }
        }
    }

    void StopPlayback(int key, KeyRequest::Source src) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == key) {
                if (src == KeyRequest::Source::USER) {
                    myVoices[i].activeFromUser = false;
                }
                else {
                    myVoices[i].activeFromSequencer = false;
                }
                if (!myVoices[i].activeFromUser && !myVoices[i].activeFromSequencer) {
                    myVoices[i].gate_ = false;
                }
                break;
            }
        }
    }

    float keyToFrequency(int transpose_nn) {
        const float referenceFrequency = 440.f; // your base (A4)
        
        // treat transpose_nn=0 as "no shift", +1 = up one semitone, -1 = down one semitone
        return referenceFrequency * pow(2.f, transpose_nn / 12.f);
    }

    void setGlobalPitchFree(float val) override {
        val = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1

        float pitch;
        float inv = val < 0.f ? -1.f : 1.f;
        if(fabsf(val) < .33f) // .01x - .5x
            pitch = val * 1.484848f + .01f * inv;
        else if (fabsf(val) < .66f ) // .5x - 1x
            pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
        else // 1x - 2x
            pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;

        global_pitch_ = pitch;
        reverse = pitch < 0.f;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].player_.setReverse(pitch < 0.f);
            myVoices[i].player_.setGlobalFrequency(fabs(pitch));
        }
    }

    float setGlobalPitchQuantized(int16_t turns, float enc_pos) override {
        encoder_chunk += turns * .25f;
            if(encoder_chunk >= 1.f || encoder_chunk <= -1.f) {
                // get current semi
                encoder_chunk = round(encoder_chunk);

                float pitch = global_pitch_;
                float orig_pitch = pitch;

                // snap to fifths and octaves
                // calculate the consts via 2^(x/12) e.g. 2^(-5/12) for down 5 semis
                float mul;
                if(!reverse)
                {
                    if(fifth)
                        mul = encoder_chunk < 0 ? .667419927085f : 1.33483985417f;
                    else
                        mul = encoder_chunk < 0 ? .749153538438f : 1.49830707688f;
                }
                else
                {
                    if(fifth)
                        mul = encoder_chunk < 0 ? 1.33483985417f : .667419927085f;
                    else
                        mul = encoder_chunk < 0 ? 1.49830707688f : .749153538438f;                            
                }

                // jump, then do nothing if we've gone over the end
                pitch *= mul;
                if(pitch > 2.f || pitch < -2.f)
                    return enc_pos;

                fifth = !fifth;
                encoder_chunk = 0.f;

                // handle direction change
                if(pitch < .0625)
                {
                    // we weren't already in the turn-around zone
                    if(orig_pitch > .0625)
                    {
                        fifth = !fifth;
                        pitch = orig_pitch;
                        reverse = !reverse;
                        for (size_t i = 0; i < NUM_VOICES; ++i) {
                            myVoices[i].player_.setReverse(reverse);
                        }
                    }
                    // we were already in the zone, and we're headed over the middle
                    else if((reverse && turns > 0) || (!reverse && turns < 0))
                    {
                        fifth = !fifth;
                        pitch = orig_pitch;
                        reverse = !reverse;
                        for (size_t i = 0; i < NUM_VOICES; ++i) {
                            myVoices[i].player_.setReverse(reverse);
                        }
                    }
                }

                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setGlobalFrequency(fabs(pitch));
                }
                global_pitch_ = pitch;

                if(pitch < .5f) // 0 - .33
                    pitch = (pitch - .01) * 0.673469f;
                else if (pitch < 1.f ) // .33 - .66
                    pitch = (pitch - .5f) * .66f + .33f;
                else // .66 - 1
                    pitch = (pitch - 1.f) * .34f + .66f;
                
                return reverse ? (1.f - pitch) * .5f : pitch * .5f + .5f;
            }

        return enc_pos;
    }

    void resetGlobalPitchQuant() override {
        encoder_chunk = 0.f;
        fifth = false;
    }

    bool isKeyPlaying(size_t key) override {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == static_cast<int>(key) && myVoices[i].gate_) {
                return true;
            }
        }
        return false;
    }

    void setSample(size_t slot) override {

        void *st = sample_manager_->getNextSample(slot, 1);

        sample_start_ = st;
        sample_bytes_ = sample_manager_->getNextSampleSize(slot, 1);
        window_start_ = 0;
        window_end_ = sample_bytes_;

        updateSlices();

        voice_slot_ = slot + 1;
    }

    void saveFileToSlot(size_t slot) override {
        sample_manager_->saveSlot(slot, 1);
    }

    void deleteSlot(size_t slot) override {
        sample_manager_->deleteSlot(slot, 1);
    }

    void copySlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        sample_manager_->copySlot(src, dst, src_engine, dst_engine);
    }

    bool isValidSample(size_t slot) override {
        return sample_manager_->isValidSample(slot, 1);
    }

    void setAttack(float amount) override {
        if (amount < .008) {
            amount = .001;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env_.SetAttackTime(amount * 5.f, 1.f);
        }
    }

    void setRelease(float amount) override {
        if (amount < .008) {
            amount = .005;
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            myVoices[i].amp_env_.SetReleaseTime(amount);
        }
        stored_release_ = amount;
    }

    void setStartPoint(float amount) override {
        window_start_ = static_cast<size_t>(amount * sample_bytes_);
        updateSlices();
    }

    void setEndPoint(float amount) override {
        window_end_ = static_cast<size_t>(amount * sample_bytes_);
        updateSlices();
    }

    void setMasterCutoff(float amount) override {
        if (amount > .45 && amount < .55) {
            //dead zone
            master_cutoff_ = .5;
            setMasterRes(0.f);
        }
        else {
            master_cutoff_ = amount < .45 ? amount * 1.111111111f : (amount - .1) * 1.111111111f; //scale back
            master_cutoff_ = fclamp(master_cutoff_, 0.f, 1.f);
            if ((amount > .4 && amount < .45) || (amount > .55 && amount < .6)) {
                setMasterRes(fabsf(0.5f - amount) * 9.f - 0.45f);
            }
            else {
                setMasterRes(.45f);
            }
        }
    }

    void setMasterRes(float amount) override {
        amount = fclamp(amount, 0.f, .99f); //This has to be limited
        filter_.SetRes(amount);
    }

    void setPan(float amount) override {
        pan_target_ = amount;
    }

    void setSampleReducer(float amount) override {
        if (amount > 0.f) {
            amount = (1.0f - amount) * 0.45f;
            amount = fclamp(amount, .01f, 1.f);
            rd_l_.SetFreq(amount);
            rd_r_.SetFreq(amount);
            reduce_ = true;
        }
        else {
            reduce_ = false;
        }
    }

    void setSampleVolume(float amount) override {
        sample_vol_target_ = amount;
    }

    uint8_t getVoiceSlot() override {
        return voice_slot_;
    }

    void setLoop(float amount) override {
        if (amount > .5) {
            if (loop == false) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setLoop(true);
                }
                loop = true;
            }
        }
        else {
            if (loop == true) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].player_.setLoop(false);
                }
                loop = false;
            }
        }
    }

    void setSustain(float amount) override {
        if (amount > .5f) {
            if (sustain == false) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].sustain = true;
                }
                sustain = true;
            }
        }
        else {
            if (sustain == true) {
                for (size_t i = 0; i < NUM_VOICES; ++i) {
                    myVoices[i].sustain = false;
                }
            }
            sustain = false;
        }
    }

    void handleRetrigEnv(bool on, size_t key) override {
        size_t idx = 0;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].key == static_cast<int>(key)) {
                idx = i;
                break;
            }
        }
        if (on) {
            myVoices[idx].amp_env_.SetReleaseTime(.005);
        }
        else {
            myVoices[idx].amp_env_.SetReleaseTime(stored_release_);
        }
    }

    void updateStartEnd(float start, float end) {
        window_start_ = static_cast<size_t>(start * sample_bytes_);
        window_end_ = static_cast<size_t>(end * sample_bytes_);
        updateSlices();
    }

    void updateSlices() {
        size_t cur_sample_size_ = window_end_ - window_start_;
        size_t slice_size_ = cur_sample_size_ / kMaxSlices;

        for (size_t i = 0; i < kMaxSlices; ++i) {
            mySlices[i].start = sample_start_ + (window_start_ + i * slice_size_) * 4;
            mySlices[i].numSamples = slice_size_;
        }
        // LOOPER: each voice keeps the slice it was started with
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            const int s = myVoices[i].slice;
            if (s >= 0 && s < static_cast<int>(kMaxSlices)) {
                myVoices[i].setStartEnd(mySlices[s].start, mySlices[s].numSamples);
            }
        }
    }

    void getActiveKeys(uint16_t *activeKeys, float *nns) override {
        size_t j = 0;
        for (size_t i = 0; i < NUM_VOICES; ++i) {
            if (myVoices[i].gate_) {
                activeKeys[j] = myVoices[i].key;
                nns[j] = myVoices[i].nn;
                ++j;
            }
        }
    }

    float chokeVoice(size_t voice) override {
        if (myVoices[voice].gate_) {
            myVoices[voice].gate_ = false;
            return myVoices[voice].nn;
        }
        return -99.f;
    }

    void setLastSlice(bool t) {
        (void)t; // LOOPER: key 15 no longer alternates slices; kept for the arpeggiator's calls
    }

    SliceInfo mySlices[kMaxSlices];

    uint8_t voice_slot_;
    float sample_vol_, sample_vol_target_;
    float global_pitch_;
    bool reverse, fifth;
    float encoder_chunk;
    float master_cutoff_;
    float pan_, pan_target_;

    private:
    sampleManager *sample_manager_;
    daisysp::Oscillator osc;
    bool loop, sustain;
    bool reduce_;
    int slice_offset_; // LOOPER: 0 or 1

    DjFilter filter_;
    SampleRateReducer rd_l_, rd_r_;

    void* sample_start_;
    size_t sample_bytes_;
    size_t window_start_;
    size_t window_end_;

    float stored_release_;
};