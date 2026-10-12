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
    bool    live; // a note played by hand (LiveNoteOn / LiveNoteOff), not a looped one
    uint8_t note;
};

struct Rec : Output
{
    std::vector<Hit> hits;
    double           t_ms = 0;
    LoopCore        *core = nullptr;
    void LoopNoteOn(const Event &e) override
    {
        hits.push_back({t_ms, core->GetPosition(), e.key, true, false, e.note});
    }
    void LoopNoteOff(uint8_t, int8_t key, uint8_t note, bool) override
    {
        hits.push_back({t_ms, core->GetPosition(), key, false, false, note});
    }
    void LiveNoteOn(const Event &e) override
    {
        hits.push_back({t_ms, core->GetPosition(), e.key, true, true, e.note});
    }
    void LiveNoteOff(const Event &e) override
    {
        hits.push_back({t_ms, core->GetPosition(), e.key, false, true, e.note});
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
    // first note-on / note-off of a key at or after a time (-1 if none)
    double first_on(int8_t key, double after = 0, int live = -1) const
    {
        for(auto &h : rec.hits)
            if(h.on && h.key == key && h.t_ms >= after && (live < 0 || h.live == (live == 1)))
                return h.t_ms;
        return -1;
    }
    double first_off(int8_t key, double after = 0, int live = -1) const
    {
        for(auto &h : rec.hits)
            if(!h.on && h.key == key && h.t_ms >= after && (live < 0 || h.live == (live == 1)))
                return h.t_ms;
        return -1;
    }
    size_t count(int8_t key, bool on, bool live) const
    {
        size_t n = 0;
        for(auto &h : rec.hits)
            if(h.key == key && h.on == on && h.live == live)
                n++;
        return n;
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
    // beat-2 note was 20 ms late, so it waits for the next 1/16 line:
    // beat 2 + 1/16 = t 4800 + 125 = 4925
    s.run_to(5000);
    auto b2 = s.ons_between(4915, 4935, 11);
    CHECK(b2.size() == 1, "beat 2 note on the next 16th, got %zu", b2.size());
    if(b2.size())
        CHECK(b2[0].t_ms >= 4924 && b2[0].t_ms <= 4926, "beat 2 + 1/16 at %.1f", b2[0].t_ms);
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
    s.run_to(2512); // just after beat 2 of pass 2 -> next 16th (960 units)
    s.core.NoteOn(0, 2, 0, 61, false);
    s.run_to(2522);
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
    // both repeats of key 2 should play on later passes (they quantize to the same 16th,
    // beat 2 + 1/16 of pass 3 = t 4010 + 625 = 4635)
    auto r = s.ons_between(4600, 4700, 2);
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
        CHECK(s.core.GetEvent(0).pos == 0, "late first note recorded on the beat, got %d",
              s.core.GetEvent(0).pos);
    }
    {
        Sim s;
        s.core.SetLiveQuantize(false); // check the raw (unquantized) wrap
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
    printf("-- quantize strength (live and playback)\n");
    for(int live = 0; live < 2; live++)
    {
        Sim s;
        s.core.SetLiveQuantize(live == 1);
        s.core.SetStrength(50); // 1/16 at 50%
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
        const Event  &e = s.core.GetEvent(1);
        const int32_t q = s.core.Quantized(e);
        const int32_t played = 92; // approx units after beat 2
        if(live) // pulled halfway toward the NEXT line (beat 2 + 192)
            CHECK(q > kUnitsPerBeat + played && q < kUnitsPerBeat + 192,
                  "live: pulled halfway forward to the next 16th: q %d", q);
        else // pulled halfway toward the nearest line (beat 2)
            CHECK(q > kUnitsPerBeat && q < kUnitsPerBeat + played,
                  "playback: pulled halfway toward the grid: q %d", q);
        if(live)
            CHECK(e.pos == q, "live: stored position is the quantized one");
        else
            CHECK(e.pos > q, "playback: raw timing kept (raw %d, q %d)", e.pos, q);
    }
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

static void test_live_snap()
{
    printf("-- live quantize: start stored on the next grid line, length kept\n");
    Sim s; // live is the default
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false);
    s.run_to(40);
    s.core.NoteOff(1);
    s.run_to(500 + 37); // beat 2, 37 ms late -> waits for the next 16th (768 + 192)
    s.core.NoteOn(0, 2, 0, 60, false);
    s.run_to(500 + 37 + 123);
    s.core.NoteOff(2);
    s.run_to(2003);
    s.core.ChompiTap(s.timeline());
    const Event &e = s.core.GetEvent(1);
    CHECK(e.pos == kUnitsPerBeat + 192, "stored on beat 2 + 1/16, got %d", e.pos);
    CHECK(e.dur_us > 121000 && e.dur_us < 125000, "length kept as played: %u", e.dur_us);
    s.run_to(4600);
    auto h = s.ons_between(2500, 2700, 2);
    CHECK(h.size() == 1 && h[0].t_ms >= 2624 && h[0].t_ms <= 2626, "plays on beat 2 + 1/16 of pass 2");
}

static void test_grid_change_only_new_notes()
{
    printf("-- changing the grid only affects notes played afterwards\n");
    for(int live = 0; live < 2; live++)
    {
        Sim s;
        s.core.SetLiveQuantize(live == 1);
        s.core.ChompiTap(s.timeline());
        s.core.NoteOn(0, 1, 0, 60, false);
        s.run_to(20);
        s.core.NoteOff(1);
        s.run_to(2005);
        s.core.ChompiTap(s.timeline()); // 1 bar
        s.core.ChompiTap(s.timeline()); // overdub
        // pass 2 starts at 2000. Play at +700 ms (1075 units) on the 1/16 grid -> 1152
        s.run_to(2700);
        s.core.NoteOn(0, 2, 0, 60, false);
        s.run_to(2710);
        s.core.NoteOff(2);
        s.core.SetRecordGrid(3); // switch to 1/8 triplets (256 units)
        // play at +1300 ms (1997 units): 1/8T grid -> 2048
        s.run_to(3300);
        s.core.NoteOn(0, 3, 0, 60, false);
        s.run_to(3310);
        s.core.NoteOff(3);
        const int32_t q2 = s.core.Quantized(s.core.GetEvent(1));
        const int32_t q3 = s.core.Quantized(s.core.GetEvent(2));
        CHECK(q2 == 1152, "%s: earlier note stays on its 1/16 line: %d", live ? "live" : "playback", q2);
        CHECK(q3 == 2048, "%s: new note on the 1/8T grid: %d", live ? "live" : "playback", q3);
        s.core.SetRecordGrid(kGridSixteenth); // changing back doesn't move the triplet note
        CHECK(s.core.Quantized(s.core.GetEvent(2)) == 2048, "triplet note stays put");
    }
}

static void test_all_grids()
{
    printf("-- every grid value\n");
    const int32_t expect[kNumGrids] = {768, 512, 384, 256, 192, 128, 96, 64, 0};
    for(int g = 0; g < kNumGrids; g++)
    {
        CHECK(kGridUnits[g] == expect[g], "grid %d units", g);
        if(kGridUnits[g])
            CHECK(kUnitsPerBar % kGridUnits[g] == 0, "grid %d divides the bar", g);
        CHECK(kGridNotes[g] == 0 || kUnitsPerBar / kGridNotes[g] == kGridUnits[g], "grid %d notes", g);
    }
    // 1/4 triplets: a note just after the third triplet (512 * 2 + 30) waits for the next (1536)
    Sim s;
    s.core.SetRecordGrid(1);
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false);
    s.run_to(10);
    s.core.NoteOff(1);
    s.run_to((1024 + 30) / 1.536);
    s.core.NoteOn(0, 2, 0, 60, false);
    s.run_to(800);
    s.core.NoteOff(2);
    CHECK(s.core.GetEvent(1).pos == 1536, "1/4T snap, got %d", s.core.GetEvent(1).pos);
    // grid off: raw timing stored
    s.core.SetRecordGrid(kGridOff);
    s.run_to(900);
    s.core.NoteOn(0, 3, 0, 60, false);
    s.run_to(910);
    s.core.NoteOff(3);
    const int32_t raw = s.core.GetEvent(2).pos;
    CHECK(raw % 64 != 0 || raw % 192 != 0, "off: unsnapped (%d)", raw);
}

static void test_live_sync_early()
{
    printf("-- live quantize with an early first note in sync mode\n");
    Sim s;
    s.core.SetSyncInfo(true, 700); // 68 units before the beat
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 3, 0, 60, false);
    CHECK(s.core.GetEvent(0).pos == 0, "snapped onto the beat, got %d", s.core.GetEvent(0).pos);
}

// ---------------- live notes: LoopCore sounds the notes you play ----------------

/** A 1-bar loop starting at t=0, playing from t=2000 (pass 2). 120 BPM:
 *  beat = 500 ms, 1/16 = 125 ms. */
static void one_bar_loop(Sim &s)
{
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false, true);
    s.run_to(20);
    s.core.NoteOff(1);
    s.run_to(1990);
    s.core.ChompiTap(s.timeline()); // close: 1 bar
    s.run_to(2000);
}

/** At 78.125 BPM the simulated clock moves exactly 1 unit per ms, so a hit can land
 *  exactly on a grid line. 1 beat = 768 ms, 1/16 = 192 ms, 1 bar = 3072 ms.
 *  A 1-bar loop starting at t=1 (the sim's clock starts counting on its first tick),
 *  playing from t=3073 (pass 2). */
static void one_bar_loop_exact(Sim &s)
{
    s.bpm = 78.125;
    s.step(); // t=1
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false, true);
    s.run_to(20);
    s.core.NoteOff(1);
    s.run_to(3060);
    s.core.ChompiTap(s.timeline()); // close: 1 bar
    s.run_to(3073);
}

static void test_live_hit_waits_for_next_line()
{
    printf("-- live hit: waits for the next grid line, sounds once, length kept\n");
    Sim s;
    one_bar_loop(s);
    s.core.ChompiTap(s.timeline()); // overdub
    // beat 2 of pass 2 is t=2500. Play 20 ms AFTER it: must wait for 2625.
    s.run_to(2520);
    s.core.NoteOn(0, 5, 0, 60, false, true);
    s.run_to(2580);
    CHECK(s.first_on(5) < 0, "silent before the grid line");
    CHECK(s.core.NoteOff(5), "LoopCore stops the note it's sounding"); // held 60 ms
    s.run_to(2700);
    auto h = s.ons_between(2500, 2700, 5);
    CHECK(h.size() == 1, "sounds exactly once, got %zu", h.size());
    if(h.size())
    {
        CHECK(h[0].t_ms >= 2624 && h[0].t_ms <= 2626, "on the next 16th, at %.1f", h[0].t_ms);
        CHECK(h[0].live, "sounded as a live note");
    }
    const double on = s.first_on(5), off = s.first_off(5);
    CHECK(off - on > 58 && off - on < 62, "length as held (60 ms): %.1f", off - on);
    CHECK(s.core.GetEvent(s.core.GetNumEvents() - 1).pos == kUnitsPerBeat + 192,
          "recorded where it sounded");
    CHECK(s.core.GetNumLiveNotes() == 0, "nothing left sounding");
    // next pass: same place, once, as a looped note
    s.run_to(4700);
    auto n = s.ons_between(4500, 4700, 5);
    CHECK(n.size() == 1 && n[0].t_ms >= 4624 && n[0].t_ms <= 4626 && !n[0].live,
          "next pass at 4625 from the loop");
}

static void test_live_hit_on_the_line()
{
    printf("-- live hit exactly on a line sounds right away\n");
    Sim s;
    one_bar_loop_exact(s);
    s.run_to(3841); // beat 2 of pass 2
    CHECK(s.core.GetPosition() == kUnitsPerBeat, "test setup: on beat 2 (%d)", s.core.GetPosition());
    s.core.NoteOn(0, 6, 0, 60, false, true);
    CHECK(s.first_on(6) == 3841, "immediate, at %.1f", s.first_on(6));
    s.run_to(3881);
    s.core.NoteOff(6);
    CHECK(s.first_off(6) == 3881, "released on key up, at %.1f", s.first_off(6));
    // one unit (1 ms) late: waits a whole 1/16
    s.run_to(4034); // 1/16 line at 4033
    s.core.NoteOn(0, 7, 0, 60, false, true);
    s.run_to(4300);
    CHECK(s.first_on(7) == 4225, "1 unit late -> next 16th (4225), at %.1f", s.first_on(7));
}

static void test_live_hit_while_playing_not_recorded()
{
    printf("-- live hit while just playing: quantized, not recorded\n");
    Sim s;
    one_bar_loop(s);
    const size_t n = s.core.GetNumEvents();
    s.run_to(2810); // 60 ms after the 16th at 2750 -> next is 2875
    s.core.NoteOn(0, 7, 0, 60, false, true);
    s.run_to(2830);
    s.core.NoteOff(7);
    s.run_to(3000);
    const double on = s.first_on(7);
    CHECK(on >= 2874 && on <= 2876, "on the next 16th, at %.1f", on);
    CHECK(s.core.GetNumEvents() == n, "nothing recorded");
}

static void test_live_quantize_off_is_immediate()
{
    printf("-- live quantize off: notes sound immediately, pulled to the grid on playback\n");
    Sim s;
    s.core.SetLiveQuantize(false);
    one_bar_loop(s);
    s.core.ChompiTap(s.timeline()); // overdub
    s.run_to(2520);                 // 20 ms after beat 2
    s.core.NoteOn(0, 8, 0, 60, false, true);
    CHECK(s.first_on(8) == 2520, "immediate, at %.1f", s.first_on(8));
    s.run_to(2560);
    s.core.NoteOff(8);
    CHECK(s.first_off(8) == 2560, "released on key up, at %.1f", s.first_off(8));
    // next pass: pulled back to the nearest 16th (beat 2, t=4500)
    s.run_to(4700);
    auto n = s.ons_between(4400, 4700, 8);
    CHECK(n.size() == 1 && n[0].t_ms >= 4499 && n[0].t_ms <= 4501, "playback on beat 2");
}

static void test_grid_off_is_immediate()
{
    printf("-- grid off: notes sound immediately\n");
    Sim s;
    one_bar_loop(s);
    s.core.SetRecordGrid(kGridOff);
    s.run_to(2520);
    s.core.NoteOn(0, 8, 0, 60, false, true);
    CHECK(s.first_on(8) == 2520, "immediate, at %.1f", s.first_on(8));
}

static void test_transport_stopped_is_immediate()
{
    printf("-- transport stopped / no loop: notes sound immediately\n");
    Sim s;
    s.run_to(123);
    s.core.NoteOn(0, 11, 0, 60, false, true);
    CHECK(s.first_on(11) == 123, "immediate");
    s.run_to(200);
    CHECK(s.core.NoteOff(11), "LoopCore stops it");
    CHECK(s.first_off(11) == 200, "released on key up");
    CHECK(s.core.GetNumLiveNotes() == 0, "nothing left sounding");
}

static void test_first_note_free_is_immediate()
{
    printf("-- first note while armed (internal clock) is beat 1: immediate\n");
    Sim s;
    s.run_to(77);
    s.core.ChompiTap(s.timeline());
    s.run_to(333);
    s.core.NoteOn(0, 12, 0, 60, false, true);
    CHECK(s.first_on(12) == 333, "immediate, at %.1f", s.first_on(12));
    CHECK(s.core.GetEvent(0).pos == 0, "recorded on beat 1");
}

static void test_first_note_sync()
{
    printf("-- first note while armed (MIDI clock): early waits for the beat, late sounds now\n");
    {
        Sim s;
        s.core.SetSyncInfo(true, 700); // 68 units (~44 ms) before the beat
        s.core.ChompiTap(s.timeline());
        s.core.NoteOn(0, 13, 0, 60, false, true);
        s.run_to(30);
        CHECK(s.first_on(13) < 0, "early: not yet");
        s.run_to(60);
        const double on = s.first_on(13);
        CHECK(on >= 44 && on <= 46, "early: on the beat (~44 ms), at %.1f", on);
        CHECK(s.core.GetEvent(0).pos == 0, "early: recorded on the beat");
    }
    {
        Sim s;
        s.run_to(10);
        s.core.SetSyncInfo(true, 50); // 50 units (~33 ms) after the beat
        s.core.ChompiTap(s.timeline());
        s.core.NoteOn(0, 13, 0, 60, false, true);
        CHECK(s.first_on(13) == 10, "late: sounds right away, at %.1f", s.first_on(13));
        CHECK(s.core.GetEvent(0).pos == 0, "late: recorded on the beat (nearest), got %d",
              s.core.GetEvent(0).pos);
    }
}

static void test_released_before_line()
{
    printf("-- released before its grid line: still plays, for as long as held\n");
    Sim s;
    one_bar_loop(s);
    s.core.SetRecordGrid(0); // 1/4 notes
    s.run_to(2550); // 50 ms after beat 2 -> waits for beat 3 (3000)
    s.core.NoteOn(0, 14, 0, 60, false, true);
    s.run_to(2580);
    CHECK(s.core.NoteOff(14), "LoopCore stops it"); // held 30 ms
    s.run_to(3100);
    const double on = s.first_on(14), off = s.first_off(14);
    CHECK(on >= 2999 && on <= 3001, "on beat 3, at %.1f", on);
    CHECK(off - on > 28 && off - on < 32, "length as held (30 ms): %.1f", off - on);
}

static void test_stop_while_waiting()
{
    printf("-- stop while a note waits: held -> sounds now, released -> dropped\n");
    Sim s;
    one_bar_loop(s);
    s.core.SetRecordGrid(0);
    s.run_to(2550);
    s.core.NoteOn(0, 15, 0, 60, false, true); // held
    s.core.NoteOn(0, 16, 0, 60, false, true);
    s.run_to(2560);
    s.core.NoteOff(16);                       // released
    s.run_to(2600);
    s.core.Stop();
    CHECK(s.first_on(15) == 2600, "held note sounds at stop, at %.1f", s.first_on(15));
    s.run_to(3500);
    CHECK(s.first_on(16) < 0, "released note dropped");
    s.core.NoteOff(15); // held 950 ms, sounding from 2600 -> released at 3550
    s.run_to(3600);
    CHECK(s.first_off(15) == 3550, "held length counted from when it sounded, at %.1f",
          s.first_off(15));
    CHECK(s.core.GetNumLiveNotes() == 0, "nothing left sounding");
}

static void test_tail_note_at_close()
{
    printf("-- note played just before the bar line while recording: one hit on the downbeat\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false, true);
    s.run_to(20);
    s.core.NoteOff(1);
    s.run_to(1950);
    s.core.ChompiTap(s.timeline()); // close: 1 bar, ends at 2000
    s.run_to(1980);
    s.core.NoteOn(0, 17, 0, 60, false, true);
    s.run_to(1990);
    s.core.NoteOff(17);
    s.run_to(2100);
    auto h = s.ons_between(1900, 2100, 17);
    CHECK(h.size() == 1, "one hit, got %zu", h.size());
    if(h.size())
        CHECK(h[0].t_ms >= 1999 && h[0].t_ms <= 2001, "on the downbeat, at %.1f", h[0].t_ms);
    s.run_to(4100);
    CHECK(s.ons_between(3990, 4010, 17).size() == 1, "and on the next downbeat");
}

static void test_held_note_and_looped_note_stay_apart()
{
    printf("-- a held key and a looped note on the same key don't cut each other\n");
    Sim s;
    one_bar_loop(s); // key 1 loops on every downbeat (t~2000, ~4000, ...) for 20 ms
    // hold key 1 by hand from just before the next downbeat until well after
    s.run_to(3800);
    s.core.SetRecordGrid(kGridOff); // so it sounds right away
    s.core.NoteOn(0, 1, 0, 60, false, true);
    CHECK(s.first_on(1, 3800, 1) == 3800, "live note on at 3800");
    s.run_to(4300); // the looped note plays on the downbeat and ends 20 ms later
    const double loop_on = s.first_on(1, 3990, 0), loop_off = s.first_off(1, 3990, 0);
    CHECK(loop_on >= 3999 && loop_on <= 4002, "looped note plays on the downbeat, at %.1f", loop_on);
    CHECK(loop_off - loop_on > 18 && loop_off - loop_on < 22,
          "looped note ends 20 ms later with a loop-note off: %.1f", loop_off - loop_on);
    CHECK(s.first_off(1, 3800, 1) < 0, "the held live note is still on");
    s.core.NoteOff(1);
    CHECK(s.first_off(1, 3800, 1) == 4300, "live note off only when the key comes up");
}

static void test_same_key_twice_newer_sets_length()
{
    printf("-- two quick presses of one key on the same grid line: newer sets the length\n");
    Sim s;
    one_bar_loop(s);
    s.core.SetRecordGrid(0); // 1/4: both wait for beat 3 (t=3000)
    s.run_to(2550);
    s.core.NoteOn(0, 18, 0, 60, false, true);
    s.run_to(2570);
    s.core.NoteOff(18); // held 20 ms
    s.run_to(2600);
    s.core.NoteOn(0, 18, 0, 60, false, true);
    s.run_to(2680);
    s.core.NoteOff(18); // held 80 ms
    s.run_to(3200);
    auto on = s.ons_between(2999, 3003, 18);
    CHECK(on.size() == 2 && on[0].t_ms == on[1].t_ms, "both start on beat 3 (one retrigger)");
    CHECK(s.count(18, false, true) == 1, "stopped once, got %zu", s.count(18, false, true));
    if(on.size())
    {
        const double len = s.first_off(18, on[0].t_ms, 1) - on[0].t_ms;
        CHECK(len > 78 && len < 82, "lasts the newer press's 80 ms: %.1f", len);
    }
}

static void test_lost_release_is_cleaned_up()
{
    printf("-- a key pressed again after its release went missing: no stuck note\n");
    Sim s;
    s.run_to(100);
    s.core.NoteOn(0, 19, 0, 60, false, true); // its release never arrives
    s.run_to(200);
    s.core.NoteOn(0, 19, 0, 60, false, true); // pressed again
    CHECK(s.count(19, false, true) == 1, "the old one was stopped first");
    s.run_to(250);
    CHECK(s.core.NoteOff(19), "the new one is stopped by LoopCore");
    CHECK(s.count(19, true, true) == 2 && s.count(19, false, true) == 2, "every on has an off");
    CHECK(s.core.GetNumLiveNotes() == 0, "nothing left sounding");
}

static void test_same_key_two_pitches()
{
    printf("-- one key recorded at two pitches (transposed): both notes get their own off\n");
    Sim s;
    s.core.SetRecordGrid(kGridOff); // keep the played timing
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false, true); // key 1 at transpose 0: note 60
    s.run_to(300);
    s.core.NoteOff(1);
    s.run_to(400);
    s.core.NoteOn(0, 1, 12, 72, false, true); // key 1 again, transposed an octave: note 72
    s.run_to(500);
    s.core.NoteOff(1);
    s.run_to(2000);
    s.core.ChompiTap(s.timeline()); // 1 bar
    // pass 2: note 60 plays at 2000 for 300 ms, note 72 at 2400 for 100 ms
    s.run_to(2700);
    size_t off60 = 0, off72 = 0;
    for(auto &h : s.rec.hits)
        if(!h.on && !h.live && h.t_ms >= 2000)
            (h.note == 60 ? off60 : off72)++;
    CHECK(off60 == 1 && off72 == 1, "an off for each pitch (60: %zu, 72: %zu)", off60, off72);
}

static void test_same_key_two_pitches_overlapping()
{
    printf("-- one key at two pitches, overlapping: the earlier pitch is still released\n");
    Sim s;
    s.core.SetRecordGrid(kGridOff);
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 1, 0, 60, false, true); // note 60, held 600 ms
    s.run_to(300);
    s.core.NoteOn(0, 1, 12, 72, false, true); // same key, note 72, while 60 still sounds
    s.run_to(400);
    s.core.NoteOff(1); // releases the most recent press (72)
    s.run_to(600);
    s.core.NoteOff(1); // releases 60 (its live note was superseded, so this is a no-op)
    s.run_to(2000);
    s.core.ChompiTap(s.timeline()); // 1 bar
    s.run_to(2800);
    // on playback: 60 from 2000 to 2600, 72 from 2300 to 2400. Both must end.
    double off60 = -1, off72 = -1;
    for(auto &h : s.rec.hits)
        if(!h.on && !h.live && h.t_ms >= 2000)
        {
            if(h.note == 60 && off60 < 0) off60 = h.t_ms;
            if(h.note == 72 && off72 < 0) off72 = h.t_ms;
        }
    CHECK(off72 >= 2399 && off72 <= 2401, "note 72 off at ~2400, got %.1f", off72);
    CHECK(off60 >= 2599 && off60 <= 2601, "note 60 off at ~2600 (not lost), got %.1f", off60);
}

static void test_noteoff_return_values()
{
    printf("-- NoteOff says whether LoopCore was sounding the note\n");
    Sim s;
    s.core.ChompiTap(s.timeline());
    s.core.NoteOn(0, 20, 0, 60, false); // sounded by something else (record only)
    s.run_to(50);
    CHECK(!s.core.NoteOff(20), "record-only note: caller stops it");
    CHECK(!s.core.NoteOff(21), "unknown key: caller stops it");
    s.core.NoteOn(0, 22, 0, 60, true, true); // a MIDI note LoopCore sounds (on the next 16th)
    s.run_to(60);
    CHECK(s.core.NoteOff(22), "LoopCore stops it");
    s.run_to(300);
    CHECK(s.count(22, true, true) == 1 && s.count(22, false, true) == 1, "one on, one off");
}

int main()
{
    test_live_snap();
    test_grid_change_only_new_notes();
    test_all_grids();
    test_live_sync_early();
    test_overdub_no_flam_and_held();
    test_basic_free();
    test_late_close();
    test_press_vs_release();
    test_overdub_undo();
    test_sync_snap();
    test_stop_start();
    test_strength();
    test_live_hit_waits_for_next_line();
    test_live_hit_on_the_line();
    test_live_hit_while_playing_not_recorded();
    test_live_quantize_off_is_immediate();
    test_grid_off_is_immediate();
    test_transport_stopped_is_immediate();
    test_first_note_free_is_immediate();
    test_first_note_sync();
    test_released_before_line();
    test_stop_while_waiting();
    test_tail_note_at_close();
    test_held_note_and_looped_note_stay_apart();
    test_same_key_twice_newer_sets_length();
    test_lost_release_is_cleaned_up();
    test_same_key_two_pitches();
    test_same_key_two_pitches_overlapping();
    test_noteoff_return_values();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
