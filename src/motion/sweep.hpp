#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace posmv {
enum class SweepDirectionConvention { NegativeFirst, PositiveFirst };
// Sine is the normal Sweep; Fixed holds one Offset, for commissioning checks only.
enum class SweepMode { Sine, Fixed };
struct SweepConfig {
    bool enabled = true;
    double amplitude_deg = 10;
    double cycle_seconds = 4;
    SweepDirectionConvention direction = SweepDirectionConvention::NegativeFirst;
    SweepMode mode = SweepMode::Sine;
    double fixed_offset_deg = 0;
    void validate() const {
        if (!std::isfinite(amplitude_deg) || amplitude_deg < 0 || amplitude_deg > 90 ||
            !std::isfinite(cycle_seconds) || cycle_seconds <= 0)
            throw std::invalid_argument("Amplitude must be 0..90; cycle must be finite and positive");
        if (!std::isfinite(fixed_offset_deg) || std::abs(fixed_offset_deg) > 90)
            throw std::invalid_argument("Fixed offset must be within ±90 degrees");
    }
};
inline double synthetic_roll(const SweepConfig& config, double elapsed_seconds) {
    config.validate();
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0)
        throw std::invalid_argument("Elapsed time must be finite and nonnegative");
    if (!config.enabled) return 0;
    if (config.mode == SweepMode::Fixed) return config.fixed_offset_deg;
    const double direction = config.direction == SweepDirectionConvention::NegativeFirst ? -1 : 1;
    return direction * config.amplitude_deg * std::sin(2 * std::numbers::pi *
        std::fmod(elapsed_seconds, config.cycle_seconds) / config.cycle_seconds);
}
// Fastest change of the Offset in degrees per second (the sine's slope at CENTER).
inline double peak_sweep_rate(const SweepConfig& config) {
    return config.mode == SweepMode::Fixed ? 0 : 2 * std::numbers::pi * config.amplitude_deg / config.cycle_seconds;
}
// Elapsed time at which the Offset is next back at CENTER (a fixed Offset has none: now).
inline double next_center(const SweepConfig& config, double elapsed_seconds) {
    if (config.mode == SweepMode::Fixed) return elapsed_seconds;
    const double half = config.cycle_seconds / 2;
    return std::ceil(elapsed_seconds / half) * half;
}
struct ComposedRoll { double roll_deg; bool clipped; };
inline ComposedRoll compose_roll(double real, double synthetic) {
    if (!std::isfinite(real) || !std::isfinite(synthetic))
        throw std::invalid_argument("Nonfinite motion");
    const double sum = real + synthetic;
    return {std::clamp(sum, -90.0, 90.0), sum < -90 || sum > 90};
}
// Where the beam should point, from straight down with the Offset's sign, assuming the instrument
// steers by the Roll it receives but no further than ±limit from the hull. Equals the Offset within
// the limit. A model, not a reading from the instrument.
inline double expected_beam(double real, double output, double limit) {
    return std::clamp(output, -limit, limit) - real;
}
}