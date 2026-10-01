#pragma once
#include <chrono>
#include <optional>
#include <stdexcept>
namespace posmv {
class InputFreshness {
public:
    using Clock = std::chrono::steady_clock;
    void timeout(std::chrono::milliseconds value) {
        if (value.count() <= 0) throw std::invalid_argument("Stale timeout must be positive");
        timeout_ = value;
    }
    void received(Clock::time_point now) { received_ = now; }
    bool fresh(Clock::time_point now) const { return received_ && now - *received_ < timeout_; }
    void reset() { received_.reset(); }
private:
    std::chrono::milliseconds timeout_{500};
    std::optional<Clock::time_point> received_;
};
}