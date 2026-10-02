#pragma once
#include "diagnostics/distribution.hpp"
#include "protocol/tss1.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
namespace posmv {
// Sequence number carried in Heave by tagged Simulator frames (see test_signal.hpp).
inline unsigned heave_tag(const Tss1Frame& frame) {
    return static_cast<unsigned>(std::lround(std::abs(frame.heave_m) * 100)) % 10000;
}
// Pairs tagged frames coming back from the Proxy with the frames the Simulator sent.
class TagTracker {
public:
    struct Pairing { double latency_ms{}, offset_deg{}; };
    void sent(std::uint64_t sequence, double seconds, double roll_deg) { sent_[sequence % slots_] = {seconds, roll_deg, true}; }
    std::optional<Pairing> received(unsigned tag, double seconds, double roll_deg) {
        const auto& record = sent_[tag % slots_];
        if (!record.valid || seconds < record.seconds) return std::nullopt;
        // Frames come back in order, so a jump in the tag means frames never returned.
        if (last_tag_) missing_ += (tag + slots_ - *last_tag_ - 1) % slots_;
        last_tag_ = tag;
        const Pairing pairing{(seconds - record.seconds) * 1000, roll_deg - record.roll_deg};
        latency_.add(pairing.latency_ms);
        offsets_.push_back({seconds, pairing.offset_deg});
        while (offsets_.front().seconds < seconds - window_seconds_) offsets_.pop_front();
        return pairing;
    }
    Distribution::Summary latency() const { return latency_.summary(); }
    std::uint64_t missing() const { return missing_; }
    struct SineEstimate { double amplitude_deg{}, center_deg{}; std::optional<double> period_seconds; };
    // Shape of the measured Offset over the recent window; no period for a fixed Offset.
    std::optional<SineEstimate> offset_estimate() const {
        if (offsets_.empty()) return std::nullopt;
        const auto [low, high] = std::ranges::minmax(offsets_, {}, &Sample::offset_deg);
        SineEstimate estimate{(high.offset_deg - low.offset_deg) / 2, (high.offset_deg + low.offset_deg) / 2, std::nullopt};
        if (estimate.amplitude_deg < 0.005) return estimate;  // below TSS1's 0.01° resolution: a fixed Offset
        // Average spacing of upward crossings through the centre line.
        std::optional<double> first, last;
        int crossings = 0;
        for (std::size_t i = 1; i < offsets_.size(); ++i) {
            const auto& a = offsets_[i - 1];
            const auto& b = offsets_[i];
            if (a.offset_deg < estimate.center_deg && b.offset_deg >= estimate.center_deg) {
                const double t = a.seconds + (estimate.center_deg - a.offset_deg) / (b.offset_deg - a.offset_deg) * (b.seconds - a.seconds);
                if (!first) first = t;
                last = t;
                ++crossings;
            }
        }
        if (crossings >= 2) estimate.period_seconds = (*last - *first) / (crossings - 1);
        return estimate;
    }
private:
    struct Record { double seconds{}, roll_deg{}; bool valid{}; };
    static constexpr std::size_t slots_ = 10000;
    std::vector<Record> sent_ = std::vector<Record>(slots_);
    Distribution latency_;
    std::optional<std::size_t> last_tag_;
    std::uint64_t missing_{};
    struct Sample { double seconds{}, offset_deg{}; };
    static constexpr double window_seconds_ = 20;
    std::deque<Sample> offsets_;
};
}
