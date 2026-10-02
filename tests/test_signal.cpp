#include "simulator/test_signal.hpp"
#include "simulator/tag_tracker.hpp"
#include <cmath>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
bool near(double actual, double expected, double tolerance) { return std::abs(actual - expected) <= tolerance; }
posmv::Tss1Frame parse(const std::string& text) { return posmv::Tss1Parser().feed(text).at(0); }
}
int main() {
    {
        posmv::TestSignal signal;
        signal.fixed_roll_deg = 1.29;
        signal.tag = true;
        check(posmv::test_frame(signal, 0, 42) == ":000000  0042G 0129  0000\r\n",
            "a fixed Roll frame carries that Roll and its sequence number in Heave");
    }
    {
        posmv::TestSignal signal;  // vessel rolling ±5° every 8 s
        signal.sine = true;
        signal.sine_roll.amplitude_deg = 5;
        signal.sine_roll.cycle_seconds = 8;
        check(posmv::test_frame(signal, 2.0, 0) == ":000000  0000G-0500  0000\r\n",
            "a sine Roll follows the simulated vessel roll");
    }
    {
        posmv::TagTracker tracker;
        tracker.sent(42, 1.000, 1.29);
        const auto back = parse(":000000  0042G 0447  0000\r\n");  // the Proxy added an Offset
        const auto pairing = tracker.received(posmv::heave_tag(back), 1.012, back.roll_deg);
        check(pairing && near(pairing->latency_ms, 12, 1e-6) && near(pairing->offset_deg, 3.18, 1e-9),
            "a returned frame pairs with the sent frame of the same tag, giving delay and Offset");
    }
    {
        posmv::TagTracker tracker;
        for (unsigned tag = 1; tag <= 5; ++tag) tracker.sent(tag, tag * 0.04, 0);
        for (unsigned tag : {1u, 2u, 5u}) tracker.received(tag, 1.0, 0);
        check(tracker.missing() == 2, "tags skipped between returned frames are counted as missing");
    }
    {
        posmv::TagTracker tracker;
        for (unsigned tag : {9998u, 9999u, 0u, 1u}) tracker.sent(tag, 1.0, 0);
        for (unsigned tag : {9998u, 9999u, 0u, 1u}) tracker.received(tag, 1.1, 0);
        check(tracker.missing() == 0, "tags wrap from 9999 to 0 without counting a loss");
    }
    {
        // Vessel rolls ±5° every 7 s; the Proxy adds a ±10°, 4 s Sweep; frames return 5 ms later.
        posmv::TagTracker tracker;
        const double pi = 3.14159265358979323846;
        for (unsigned i = 0; i < 1000; ++i) {
            const double t = i * 0.01;
            const double real = 5 * std::sin(2 * pi * t / 7);
            tracker.sent(i, t, real);
            tracker.received(i, t + 0.005, real + 10 * std::sin(2 * pi * t / 4 + 0.3));
        }
        const auto sweep = tracker.offset_estimate();
        check(sweep && near(sweep->amplitude_deg, 10, 0.05) && sweep->period_seconds && near(*sweep->period_seconds, 4, 0.02) &&
            near(sweep->center_deg, 0, 0.05), "the measured Offset reveals the Sweep's amplitude and period, whatever the vessel roll");
    }
    {
        posmv::TagTracker tracker;  // fixed Offset +5° for commissioning
        for (unsigned i = 0; i < 200; ++i) { tracker.sent(i, i * 0.04, -2); tracker.received(i, i * 0.04 + 0.005, 3); }
        const auto fixed = tracker.offset_estimate();
        check(fixed && near(fixed->amplitude_deg, 0, 1e-9) && near(fixed->center_deg, 5, 1e-9) && !fixed->period_seconds,
            "a fixed Offset shows as a constant with no period");
    }
    {
        posmv::TagTracker tracker;  // 7.34 - 2.34 is not exactly 5 in floating point
        for (unsigned i = 0; i < 200; ++i) {
            tracker.sent(i, i * 0.04, 0);
            tracker.received(i, i * 0.04 + 0.005, i % 2 ? 5.000000000000001 : 4.999999999999999);
        }
        const auto fixed = tracker.offset_estimate();
        check(fixed && !fixed->period_seconds, "noise below the TSS1 resolution is not mistaken for a sine");
    }
    return failures ? 1 : 0;
}
