#pragma once
#include "motion/sweep.hpp"
#include "protocol/tss1.hpp"
#include <cstdint>
#include <string>
namespace posmv {
// Frames the Simulator generates: zero accelerations and pitch, status G, Roll fixed or sine.
// With tagging, Heave carries a sequence number (0-9999 cm) that the Proxy passes through
// untouched, so the receiver can pair each output frame with the frame it came from.
struct TestSignal {
    bool sine = false;
    double fixed_roll_deg = 0;
    SweepConfig sine_roll;  // amplitude and cycle of the simulated vessel roll
    bool tag = false;
};
inline std::string test_frame(const TestSignal& signal, double elapsed_seconds, std::uint64_t sequence) {
    Tss1Frame frame{":000000  0000G 0000  0000", 0, 0, 0, 'G'};
    if (signal.tag) {
        auto digits = std::to_string(sequence % 10000);
        frame.raw.replace(9, 4, std::string(4 - digits.size(), '0') + digits);  // Heave field, cm
    }
    auto vessel = signal.sine_roll;
    vessel.enabled = true;
    vessel.mode = SweepMode::Sine;
    return encode_roll(frame, signal.sine ? synthetic_roll(vessel, elapsed_seconds) : signal.fixed_roll_deg);
}
}
