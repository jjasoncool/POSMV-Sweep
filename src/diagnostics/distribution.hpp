#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace posmv {
// Running min/mean/max and p99 of millisecond values, in bounded memory.
class Distribution {
public:
    struct Summary { std::uint64_t count{}; double min_ms{}, mean_ms{}, max_ms{}, p99_ms{}; };
    void add(double ms) {
        summary_.min_ms = summary_.count ? std::min(summary_.min_ms, ms) : ms;
        summary_.max_ms = std::max(summary_.max_ms, ms);
        ++summary_.count;
        total_ms_ += ms;
        summary_.mean_ms = total_ms_ / summary_.count;
        ++histogram_[std::min<std::size_t>(static_cast<std::size_t>(std::max(ms, 0.0) / bin_ms_), bins_)];
    }
    Summary summary() const {
        auto result = summary_;
        // Centre of the 0.1 ms bin holding the ceil(0.99 n)-th value; the overflow bin reports the maximum.
        const auto rank = static_cast<std::uint64_t>(std::ceil(0.99 * static_cast<double>(summary_.count)));
        std::uint64_t seen = 0;
        for (std::size_t bin = 0; bin < histogram_.size() && rank; ++bin)
            if ((seen += histogram_[bin]) >= rank) {
                result.p99_ms = bin == bins_ ? summary_.max_ms : (static_cast<double>(bin) + 0.5) * bin_ms_;
                break;
            }
        return result;
    }
private:
    static constexpr double bin_ms_ = 0.1;
    static constexpr std::size_t bins_ = 10000;  // up to 1000 ms, plus one overflow bin
    Summary summary_;
    double total_ms_{};
    std::vector<std::uint32_t> histogram_ = std::vector<std::uint32_t>(bins_ + 1);
};
}
