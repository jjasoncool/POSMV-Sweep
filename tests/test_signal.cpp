#include "simulator/test_signal.hpp"
#include "simulator/frame_pairing.hpp"
#include "transport/serial_transport.hpp"
#include "scheduler/output_scheduler.hpp"
#include <cmath>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
bool near(double actual, double expected, double tolerance) { return std::abs(actual - expected) <= tolerance; }
posmv::Tss1Frame parse(const std::string& text) { return posmv::Tss1Parser().feed(text).at(0); }
// A Simulator frame with a fixed Roll and `tag` in Heave.
posmv::Tss1Frame tagged(unsigned tag, double roll) {
    posmv::TestSignal signal;
    signal.fixed_roll_deg = roll;
    signal.tag = true;
    return parse(posmv::test_frame(signal, 0, tag));
}
// The same frame after the Proxy rewrote its Roll.
posmv::Tss1Frame with_roll(const posmv::Tss1Frame& frame, double roll) { return parse(posmv::encode_roll(frame, roll)); }
}
int main() {
    {
        posmv::FramePairing pairing;
        const auto sent = tagged(42, 1.29);
        pairing.sent(sent, 1.000);
        const auto back = pairing.received(with_roll(sent, 4.47), 1.012);  // the Proxy added an Offset
        check(back && near(back->latency_ms, 12, 1e-6) && near(back->offset_deg, 3.18, 1e-9) && !back->identical,
            "a returned frame pairs with the sent frame that matches it apart from Roll, giving delay and Offset");
    }
    {
        posmv::FramePairing pairing;
        for (unsigned tag = 1; tag <= 5; ++tag) pairing.sent(tagged(tag, 0), tag * 0.04);
        for (unsigned tag : {1u, 2u, 5u}) pairing.received(tagged(tag, 0), 1.0);
        check(pairing.missing() == 2, "sent frames skipped by the returned ones are counted as missing");
    }
    {
        posmv::FramePairing pairing;
        for (unsigned tag : {9998u, 9999u, 0u, 1u}) pairing.sent(tagged(tag, 0), 1.0);
        for (unsigned tag : {9998u, 9999u, 0u, 1u}) pairing.received(tagged(tag, 0), 1.1);
        check(pairing.missing() == 0, "tags wrapping from 9999 to 0 are not a loss");
    }
    {
        // Vessel rolls ±5° every 7 s; the Proxy adds a ±10°, 4 s Sweep; frames return 5 ms later.
        posmv::FramePairing pairing;
        const double pi = 3.14159265358979323846;
        for (unsigned i = 0; i < 1000; ++i) {
            const double t = i * 0.01;
            const double real = 5 * std::sin(2 * pi * t / 7);
            const auto sent = tagged(i, real);
            pairing.sent(sent, t);
            pairing.received(with_roll(sent, sent.roll_deg + 10 * std::sin(2 * pi * t / 4 + 0.3)), t + 0.005);
        }
        const auto sweep = pairing.offset_estimate();
        check(sweep && near(sweep->amplitude_deg, 10, 0.05) && sweep->period_seconds && near(*sweep->period_seconds, 4, 0.02) &&
            near(sweep->center_deg, 0, 0.05), "the measured Offset reveals the Sweep's amplitude and period, whatever the vessel roll");
    }
    {
        posmv::FramePairing pairing;  // fixed Offset +5° for commissioning
        for (unsigned i = 0; i < 200; ++i) {
            const auto sent = tagged(i, -2);
            pairing.sent(sent, i * 0.04);
            pairing.received(with_roll(sent, 3), i * 0.04 + 0.005);
        }
        const auto fixed = pairing.offset_estimate();
        check(fixed && near(fixed->amplitude_deg, 0, 1e-9) && near(fixed->center_deg, 5, 1e-9) && !fixed->period_seconds,
            "a fixed Offset shows as a constant with no period");
    }
    {
        posmv::FramePairing pairing;  // e.g. 7.34 - 2.34 is not exactly 5 in floating point
        for (unsigned i = 0; i < 200; ++i) {
            const auto sent = tagged(i, (i % 7) * 1.17 - 3);
            pairing.sent(sent, i * 0.04);
            pairing.received(with_roll(sent, sent.roll_deg + 5), i * 0.04 + 0.005);
        }
        const auto fixed = pairing.offset_estimate();
        check(fixed && !fixed->period_seconds && near(fixed->center_deg, 5, 1e-9),
            "noise below the TSS1 resolution is not mistaken for a sine");
    }
    const auto a = parse(":00000A -0355G-0053  0129\r\n");  // frames from the supplied POS MV capture
    const auto b = parse(":01001D  0012G 0045  0177\r\n");
    {
        posmv::FramePairing pairing;  // a looping capture repeats its frames
        for (const auto& frame : {a, b, a}) pairing.sent(frame, 1.0);
        for (const auto& frame : {a, b, a}) pairing.received(frame, 1.1);
        check(pairing.paired() == 3 && pairing.missing() == 0, "capture frames with repeated contents pair in order");
    }
    {
        posmv::FramePairing pairing;
        pairing.sent(a, 1.0);
        pairing.received(b, 1.1);
        pairing.received(a, 1.2);
        check(pairing.unmatched() == 1 && pairing.paired() == 1 && pairing.missing() == 0,
            "a returned frame that matches nothing sent is unmatched and leaves the pairing intact");
    }
    {
        posmv::FramePairing pairing;
        pairing.sent(a, 1.0);
        pairing.sent(b, 1.04);
        pairing.received(a, 1.1);
        pairing.received(with_roll(b, 5.45), 1.14);
        check(pairing.identical() == 1, "with Sweep OFF a returned frame is byte for byte the one sent");
    }
    {
        posmv::FramePairing pairing;  // the Proxy was disconnected while the capture looped
        pairing.sent(a, 0.0);
        pairing.sent(a, 6.0);
        const auto back = pairing.received(a, 6.01);
        check(back && near(back->latency_ms, 10, 1e-6) && pairing.missing() == 1,
            "a frame not back within 5 s is missing, so a later identical frame pairs with its own send");
    }
    // 27 bytes x 10 bits per frame: 6750, 13500, 27000 and 54000 bit/s; POS MV recommends 9600 at 25 Hz.
    check(posmv::minimum_baud(25) == 9600 && posmv::minimum_baud(50) == 19200 && posmv::minimum_baud(100) == 38400 &&
        posmv::minimum_baud(200) == 57600, "each rate gets the lowest standard baud that carries it");
    {
        bool accepted = true;
        for (unsigned rate : {25u, 50u, 100u, 200u}) {
            try { posmv::OutputScheduler scheduler(rate); } catch (const std::exception&) { accepted = false; }
        }
        check(accepted, "the Simulator can send at 25, 50, 100 and 200 Hz");
    }
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
    return failures ? 1 : 0;
}
