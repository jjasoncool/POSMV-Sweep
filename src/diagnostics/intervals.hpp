#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <cmath>
namespace posmv {
// Timestamps are monotonic seconds observed at receiver, not source timestamps.
class Intervals {
public:
    void observe(double seconds) {
        if (!std::isfinite(seconds)) return;
        if (last_) {
            const double ms = (seconds - *last_) * 1000;
            if (ms < 0) return;
            minimum_ms = intervals == 0 ? ms : std::min(minimum_ms, ms);
            maximum_ms = std::max(maximum_ms, ms);
            total_ms_ += ms; ++intervals;
            average_ms = total_ms_ / intervals;
        }
        last_ = seconds;
    }
    std::uint64_t intervals{};
    double minimum_ms{}, maximum_ms{}, average_ms{};
private:
    std::optional<double> last_;
    double total_ms_{};
};
}