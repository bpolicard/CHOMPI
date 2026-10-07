// Host tests for LoopCore. From this folder:
//   g++ -std=c++17 -Wall -Wextra -O1 -o test_loopcore test_loopcore.cpp && ./test_loopcore
#include "../LoopCore.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace looper;

struct Hit
{
    double  t_ms;
    int32_t pos;
    int8_t  key;
    bool    on;
};

struct Rec : Output
{
    std::vector<Hit> hits;
    double           t_ms = 0;
    LoopCore        *core = nullptr;
    void LoopNoteOn(const Event &e) override
    {
        hits.push_back({t_ms, core->GetPosition(), e.key, true});
    }
    void LoopNoteOff(uint8_t, int8_t key, uint8_t, bool) override
    {
        hits.push_back({t_ms, core->GetPosition(), key, false});
    }
};

static int failures = 0;
#define CHECK(cond, ...)                         \
    do                                           \
    {                                            \
        if(!(cond))                              \
        {                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
            failures++;                          \
        }                                        \
    } while(0)

// 120 BPM: one beat = 500 ms = 768 units
struct Sim
{
    LoopCore core;
    Rec      rec;
    double   t_ms     = 0;
    double   units    = 0;
    double   bpm      = 120;
    Sim()
    {
        rec.core = &core;
        core.Init(&rec);
    }
    uint32_t timeline() const { return static_cast<uint32_t>(units); }
    void     step(double ms = 1.0)
    {
        t_ms += ms;
        units += ms * (bpm / 60.0) * kUnitsPerBeat / 1000.0;
        rec.t_ms = t_ms;
        core.Process(timeline(), static_cast<uint32_t>(t_ms * 1000.0));
    }
    void run_to(double ms)
    {
        while(t_ms + 1e-9 < ms)
            step();
    }
    std::vector<Hit> ons_between(double a, double b, int8_t key = -1) const
    {
        std::vector<Hit> v;
        for(auto &h : rec.hits)
            if(h.on && h.t_ms >= a && h.t_ms < b && (key < 0 || h.key == key))
                v.push_back(h);
        return v;
    }
};

static void test_basic_free()
{
    printf("-- basic free-clock loop\n");
    Sim s;
    s.run_to(100);
    s.core.ChompiTap(s.timeline());
    CHECK(s.core.GetState() == State::ARMED, "armed");
    s.run_to(300); // nothing happens while armed
    // first note at t=300 (beat 1); held 100 ms
    s.core.NoteOn(0, 10, 0, 60, false);
    CHECK(s.core.GetState() == State::RECORDING, "recording");
    s.run_to(400);
    s.core.NoteOff(10);
    // note on beat 2 (t=800), slightly late (+20 ms), held 50 ms
    s.run_to(820);
    s.core.NoteOn(0, 11, 2, 62, false);
    s.run_to(870);
    s.core.NoteOff(11);
    // close just before the 2-bar line (2 bars = 4000 ms -> t=4300), tap at 4250
    s.run_to(4250);
    s.core.ChompiTap(s.timeline());
    CHECK(s.core.GetLength() == 2 * kUnitsPerBar, "len 2 bars, got %d", s.core.GetLength());
    CHECK(s.core.GetState() == State::RECORDING, "still recording tail");
    // a note played in the tail (t=4280) must be recorded
    s.run_to(4280);
    s.core.NoteOn(0, 12, 4, 64, false);
    s.run_to(4290);
    s.core.NoteOff(12);
    s.run_to(4302);
    CHECK(s.core.GetState() == State::PLAYING, "playing after bar line");
    // downbeat at wrap (t≈4300)
    auto d = s.ons_between(4295, 4305, 10);
    CHECK(d.size() == 1, "downbeat fired on wrap, got %zu", d.size());
    // beat-2 note quantized to t=4800 (1/16 grid at 100%)
    s.run_to(4900);
    auto b2 = s.ons_between(4790, 4810, 11);
    CHECK(b2.size() == 1, "beat 2 note quantized, got %zu", b2.size());
    if(b2.size())
        CHECK(b2[0].t_ms >= 4799 && b2[0].t_ms <= 4801, "beat 2 at %.1f", b2[0].t_ms);
    // tail note (played at 4280 = 20 ms before the loop end) quantizes to the
    // downbeat; it was heard live at 4280, so it must NOT flam at 4300...
    auto tail_now = s.ons_between(4295, 4305, 12);
    CHECK(tail_now.size() == 0, "no flam on tail note");
    // ...but it plays on the next pass's downbeat (t=8300)
    s.run_to(8400);
    auto tail_next = s.ons_between(8295, 8305, 12);
    CHECK(tail_next.size() == 1, "tail note on next downbeat, got %zu", tail_next.size());
    // note-off timing: key 10 held 100 ms
    double on_t = -1, off_t = -1;
    for(auto &h : s.rec.hits)
    {
        if(h.key == 10 && h.on && h.t_ms > 4200 && on_t < 0)
            on_t = h.t_ms;
        if(h.key == 10 && !h.on && on_t > 0 && off_t < 0)
            off_t = h.t_ms;
    }
    CHECK(off_t - on_t > 98 && off_t - on_t < 103, "duration %.1f", off_t - on_t);
}

static void test_late_close()
{
    printf("-- late close rounds down\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.run_to(10);
    s.core.NoteOn(1, 20, 0, 36, false);
    s.run_to(60);
    s.core.NoteOff(20);
    // 1 bar = 2000 ms; tap 200 ms late (t=2210)
    s.run_to(2210);
    s.core.ChompiTap(s.timeline());
    CHECK(s.core.GetLength() == kUnitsPerBar, "1 bar, got %d", s.core.GetLength());
    CHECK(s.core.GetState() == State::PLAYING, "playing immediately");
    // next downbeat at t=4010
    s.run_to(4100);
    auto d = s.ons_between(4005, 4015, 20);
    CHECK(d.size() == 1, "downbeat at 4010, got %zu", d.size());
}

static void test_press_vs_release()
{
    printf("-- close uses the press time, not the release\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.run_to(10);
    s.core.NoteOn(0, 5, 0, 60, false);
    s.run_to(20);
    s.core.NoteOff(5);
    // pressed at 2900 (1.45 bars -> rounds to 1), processed 300 ms later
    s.run_to(2900);
    const uint32_t press = s.timeline();
    s.run_to(3200);
    s.core.ChompiTap(press);
    CHECK(s.core.GetLength() == kUnitsPerBar, "rounded from press time, got %d", s.core.GetLength());
}

static void test_overdub_undo()
{
    printf("-- overdub, undo, clear\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.run_to(10);
    s.core.NoteOn(0, 1, 0, 60, false);
    s.run_to(20);
    s.core.NoteOff(1);
    s.run_to(2005);
    s.core.ChompiTap(s.timeline()); // 1 bar
    s.run_to(2100);
    s.core.ChompiTap(s.timeline()); // open overdub
    CHECK(s.core.GetState() == State::OVERDUB, "overdub");
    s.run_to(2510);
    s.core.NoteOn(0, 2, 0, 61, false);
    s.run_to(2520);
    s.core.NoteOff(2);
    s.core.NoteOn(0, 2, 0, 61, false); // same key again (repeat)
    s.run_to(2530);
    s.core.NoteOff(2);
    s.run_to(2600);
    s.core.ChompiTap(s.timeline()); // close overdub
    CHECK(s.core.GetState() == State::PLAYING, "playing");
    CHECK(s.core.GetLength() == kUnitsPerBar, "length unchanged");
    CHECK(s.core.GetNumEvents() == 3, "3 events, got %zu", s.core.GetNumEvents());
    s.run_to(6100);
    // both repeats of key 2 should play on later passes (they quantize to the same 16th)
    auto r = s.ons_between(4400, 4600, 2);
    CHECK(r.size() == 2, "repeated note kept, got %zu", r.size());
    s.core.Undo();
    CHECK(s.core.GetNumEvents() == 1, "undo removed overdub, got %zu", s.core.GetNumEvents());
    s.core.Undo();
    CHECK(s.core.GetState() == State::EMPTY, "undo of first take clears");
}

static void test_sync_snap()
{
    printf("-- sync: start snaps to nearest beat\n");
    {
        Sim s;
        s.core.SetSyncInfo(true, 50); // 50 units after a beat (late)
        s.core.ChompiTap(s.timeline());
        s.core.NoteOn(0, 3, 0, 60, false);
        CHECK(s.core.GetPosition() == 50, "late note sits at +50, got %d", s.core.GetPosition());
    }
    {
        Sim s;
        s.core.SetSyncInfo(true, 700); // 68 units before the next beat (early)
        s.core.ChompiTap(s.timeline());
        s.core.NoteOn(0, 3, 0, 60, false);
        CHECK(s.core.GetPosition() == -68, "early note sits at -68, got %d", s.core.GetPosition());
        s.core.NoteOff(3);
        s.run_to(2100);
        s.core.ChompiTap(s.timeline());
        s.run_to(2200);
        // event should have wrapped to the end of the loop and quantize to 0
        CHECK(s.core.GetNumEvents() == 1, "1 event");
        CHECK(s.core.GetEvent(0).pos == kUnitsPerBar - 68, "wrapped pos %d", s.core.GetEvent(0).pos);
        CHECK(s.core.Quantized(s.core.GetEvent(0)) == 0, "quantizes to downbeat");
    }
}

static void test_stop_start()
{
    printf("-- stop / start from the top\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.run_to(5);
    s.core.NoteOn(0, 7, 0, 60, false);
    s.run_to(15);
    s.core.NoteOff(7);
    s.run_to(2005);
    s.core.ChompiTap(s.timeline());
    s.run_to(3000);
    s.core.Stop();
    const size_t before = s.rec.hits.size();
    s.run_to(5000);
    CHECK(s.rec.hits.size() == before, "silent while stopped");
    s.core.Start();
    s.step();
    auto d = s.ons_between(5000, 5002, 7);
    CHECK(d.size() == 1, "downbeat on restart, got %zu", d.size());
}

static void test_strength()
{
    printf("-- quantize strength\n");
    Sim s;
    s.core.SetQuantize(kUnitsPerBeat / 4, 50); // 1/16 at 50%
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false);
    s.run_to(50);
    s.core.NoteOff(1);
    s.run_to(500 + 60); // beat 2 + 60 ms (~92 units late; 16th = 192)
    s.core.NoteOn(0, 2, 0, 60, false);
    s.run_to(600);
    s.core.NoteOff(2);
    s.run_to(2002);
    s.core.ChompiTap(s.timeline());
    const Event &e = s.core.GetEvent(1);
    const int32_t q = s.core.Quantized(e);
    CHECK(q > kUnitsPerBeat && q < e.pos, "pulled halfway toward the grid: raw %d q %d", e.pos, q);
}


static void test_overdub_no_flam_and_held()
{
    printf("-- overdub: no double hit, held notes wait for release\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.run_to(10);
    s.core.NoteOn(0, 1, 0, 60, false);
    s.run_to(20);
    s.core.NoteOff(1);
    s.run_to(2005);
    s.core.ChompiTap(s.timeline()); // 1-bar loop starting t=10
    s.core.ChompiTap(s.timeline()); // overdub
    // play 30 ms EARLY for beat 3 (beat 3 = t 1010 + 2000 = 3010): played at 2980
    s.run_to(2980);
    s.core.NoteOn(0, 4, 0, 60, false);
    s.run_to(2990);
    s.core.NoteOff(4);
    // it quantizes to 3010 in this same pass -> must not double-hit at 3010
    s.run_to(3100);
    CHECK(s.ons_between(3000, 3020, 4).empty(), "no double hit right after live note");
    s.run_to(5100);
    CHECK(s.ons_between(5000, 5020, 4).size() == 1, "plays on the next pass");
    // hold a note across the loop boundary
    s.run_to(5500);
    s.core.NoteOn(0, 6, 0, 60, false);
    s.run_to(8000); // still held after a full pass: must not be replayed yet
    CHECK(s.ons_between(7400, 7600, 6).empty(), "held note not replayed while held");
    s.core.NoteOff(6);
    s.run_to(9600);
    CHECK(s.ons_between(9400, 9600, 6).size() == 1, "replays after release");
}

int main()
{
    test_overdub_no_flam_and_held();
    test_basic_free();
    test_late_close();
    test_press_vs_release();
    test_overdub_undo();
    test_sync_snap();
    test_stop_start();
    test_strength();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
