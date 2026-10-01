#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace posmv {
enum class SweepDirectionConvention { NegativeFirst, PositiveFirst };
struct SweepConfig {
    bool enabled = true;
    double amplitude_deg = 10;
    double cycle_seconds = 4;
    SweepDirectionConvention direction = SweepDirectionConvention::NegativeFirst;
    void validate() const {
        if (!std::isfinite(amplitude_deg) || amplitude_deg < 0 || amplitude_deg > 90 ||
            !std::isfinite(cycle_seconds) || cycle_seconds <= 0)
            throw std::invalid_argument("Amplitude must be 0..90; cycle must be finite and positive");
    }
};
inline double synthetic_roll(const SweepConfig& config, double elapsed_seconds) {
    config.validate();
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0)
        throw std::invalid_argument("Elapsed time must be finite and nonnegative");
    if (!config.enabled) return 0;
    const double direction = config.direction == SweepDirectionConvention::NegativeFirst ? -1 : 1;
    return direction * config.amplitude_deg * std::sin(2 * std::numbers::pi *
        std::fmod(elapsed_seconds, config.cycle_seconds) / config.cycle_seconds);
}
struct ComposedRoll { double roll_deg; bool clipped; };
inline ComposedRoll compose_roll(double real, double synthetic) {
    if (!std::isfinite(real) || !std::isfinite(synthetic))
        throw std::invalid_argument("Nonfinite motion");
    const double sum = real + synthetic;
    return {std::clamp(sum, -90.0, 90.0), sum < -90 || sum > 90};
}
}