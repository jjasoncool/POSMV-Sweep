#pragma once
#include "diagnostics/distribution.hpp"
#include "protocol/tss1.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
namespace posmv {
// Pairs frames coming back from the Proxy with the frames the Simulator sent. The Proxy may
// change only Roll, so the other bytes identify a frame, whether generated or from a capture.
class FramePairing {
public:
    struct Pairing { double latency_ms{}, offset_deg{}; bool identical{}; };
    void sent(const Tss1Frame& frame, double seconds) {
        expire(seconds);
        pending_.push_back({key(frame.raw), frame.raw, seconds, frame.roll_deg});
    }
    std::optional<Pairing> received(const Tss1Frame& frame, double seconds) {
        expire(seconds);
        const auto match = std::ranges::find(pending_, key(frame.raw), &Sent::key);
        if (match == pending_.end()) { ++unmatched_; return std::nullopt; }
        const Pairing pairing{(seconds - match->seconds) * 1000, frame.roll_deg - match->roll_deg, frame.raw == match->raw};
        // Frames come back in order, so frames sent before the match never came back.
        missing_ += match - pending_.begin();
        pending_.erase(pending_.begin(), match + 1);
        ++paired_;
        if (pairing.identical) ++identical_;
        latency_.add(pairing.latency_ms);
        offsets_.push_back({seconds, pairing.offset_deg});
        while (offsets_.front().seconds < seconds - window_seconds_) offsets_.pop_front();
        return pairing;
    }
    Distribution::Summary latency() const { return latency_.summary(); }
    std::uint64_t paired() const { return paired_; }
    std::uint64_t identical() const { return identical_; }  // byte for byte the frame sent
    std::uint64_t missing() const { return missing_; }      // sent, never came back
    std::uint64_t unmatched() const { return unmatched_; }  // came back, matching nothing sent
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
    struct Sent { std::string key, raw; double seconds{}, roll_deg{}; };
    struct Sample { double seconds{}, offset_deg{}; };
    // Every byte except the Roll field (columns 14-18).
    static std::string key(std::string raw) { return raw.replace(14, 5, 5, '*'); }
    // A frame not back within 5 s is lost; a looping capture repeats it later, and that copy must
    // pair with its own send.
    void expire(double now) {
        while (!pending_.empty() && pending_.front().seconds < now - expiry_seconds_) { pending_.pop_front(); ++missing_; }
    }
    static constexpr double window_seconds_ = 20, expiry_seconds_ = 5;
    std::deque<Sent> pending_;
    std::uint64_t paired_{}, identical_{}, missing_{}, unmatched_{};
    Distribution latency_;
    std::deque<Sample> offsets_;
};
}
