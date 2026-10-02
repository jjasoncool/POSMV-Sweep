#pragma once
#include "motion/sweep.hpp"
#include "protocol/tss1.hpp"
#include "transport/serial_transport.hpp"
#include "diagnostics/timing_stats.hpp"
#include "app/input_freshness.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace posmv {
enum class ProxyState { Disconnected, Waiting, Active, Stale, Error };
struct LinkTiming {
    double current_hz{}, average_hz{};
    std::uint64_t gaps{};
    TimingStats::Intervals intervals;
};
struct ProxySnapshot {
    ProxyState state = ProxyState::Disconnected;
    bool connected{}, stale{}, sweep_enabled{};
    bool sweep_stopped_by_stale{};  // Sweep was switched OFF by stale input and not re-enabled since
    std::string error;
    double real{}, offset{}, output{};
    LinkTiming input_timing, output_timing;
    std::uint64_t input_count{}, output_count{}, invalid{}, clipped{};
    double forward_mean_ms{}, forward_max_ms{};  // from a frame's arrival until it is written
    bool over_limit{};                           // latest output Roll beyond the MBES stabilisation limit
    std::uint64_t over_limit_count{};
    bool sweep_finishing{};                      // OFF requested; the sine runs on to the next CENTER
    bool apply_pending{};                        // new settings take effect at the next CENTER
};
// Forwards every valid input frame as soon as it arrives: unchanged with Sweep OFF, Roll rewritten
// when ON. MBES time-tag serial attitude on arrival, so forwarding at once keeps the added delay
// small and steady (see docs/research/2026-10-02-mbes-motion-discontinuities.md).
class ProxyEngine {
public:
    using Clock = std::chrono::steady_clock;
    ~ProxyEngine() { stop(); }
    void configure(SweepConfig config) {
        config.validate(); std::lock_guard lock(mutex_);
        if (config.enabled && state_.stale) throw std::runtime_error("資料過期中，無法開啟 Sweep");
        if (finish_at_center_ && config.enabled && config_.enabled && config_.mode == SweepMode::Sine && !state_.sweep_finishing) {
            pending_ = config;
            apply_at_ = next_center(config_, std::chrono::duration<double>(Clock::now() - sweep_start_).count());
            state_.apply_pending = true;
            return;
        }
        config_ = config; sweep_start_ = Clock::now();
        state_.sweep_enabled = config.enabled;
        if (config.enabled) state_.sweep_stopped_by_stale = false;
    }
    void enable(bool enabled) {
        std::lock_guard lock(mutex_);
        if (enabled && state_.stale) throw std::runtime_error("資料過期中，無法開啟 Sweep");
        if (enabled && state_.sweep_finishing) { stop_at_.reset(); state_.sweep_finishing = false; return; }  // keep running
        if (!enabled && state_.sweep_finishing) return;
        if (!enabled && finish_at_center_ && config_.enabled && config_.mode == SweepMode::Sine) {
            stop_at_ = next_center(config_, std::chrono::duration<double>(Clock::now() - sweep_start_).count());
            state_.sweep_finishing = true;
            return;
        }
        if (enabled && !config_.enabled) sweep_start_ = Clock::now();
        config_.enabled = enabled; state_.sweep_enabled = enabled;
        if (enabled) state_.sweep_stopped_by_stale = false;
    }
    // Largest |Roll| the downstream MBES can stabilise; beyond it the designed angle is not reached.
    void stabilisation_limit(double degrees) {
        if (!std::isfinite(degrees) || degrees <= 0 || degrees > 90) throw std::invalid_argument("穩定上限必須在 0–90° 之間");
        std::lock_guard lock(mutex_); limit_deg_ = degrees;
    }
    // Operator option: OFF and Apply wait for the next CENTER instead of jumping the Offset to 0.
    void finish_at_center(bool on) { std::lock_guard lock(mutex_); finish_at_center_ = on; }
    void stale_timeout(std::chrono::milliseconds value) {
        std::lock_guard lock(mutex_); freshness_.timeout(value); gap_seconds_ = value.count() / 1000.0;
    }
    ProxySnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        auto s = state_;
        const double now = seconds(Clock::now());
        s.input_timing = timing(input_timing_, now);
        s.output_timing = timing(output_timing_, now);
        s.state = !s.error.empty() ? ProxyState::Error : !s.connected ? ProxyState::Disconnected
            : s.stale ? ProxyState::Stale : !latest_ ? ProxyState::Waiting : ProxyState::Active;
        return s;
    }
    void stop() {
        worker_.request_stop();
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_); state_.connected = false;
    }
    void start(const std::string& input, const std::string& output, unsigned baud) {
        if (input == output) throw std::invalid_argument("輸入與輸出 COM 不能相同");
        stop();
        start(std::make_shared<SerialTransport>(input, baud), std::make_shared<SerialTransport>(output, baud));
    }
    void start(std::shared_ptr<ITransport> input, std::shared_ptr<ITransport> output) {
        stop();
        {
            std::lock_guard lock(mutex_);
            if (pending_) config_ = *pending_;
            state_ = {}; state_.connected = true; latest_.reset(); freshness_.reset();
            stop_at_.reset(); apply_at_.reset(); pending_.reset();
            start_ = sweep_start_ = Clock::now(); state_.sweep_enabled = config_.enabled;
            // A pause longer than the Stale Timeout is a gap, not an interval.
            input_timing_ = TimingStats(gap_seconds_); output_timing_ = TimingStats(gap_seconds_);
            forward_total_ms_ = 0;
        }
        worker_ = std::jthread([this, input, output](std::stop_token token) { forward(token, *input, *output); });
    }
private:
    void forward(std::stop_token token, ITransport& input, ITransport& output) {
        try {
            Tss1Parser parser; std::array<char, 4096> bytes{};
            while (!token.stop_requested()) {
                const auto n = input.read(bytes);
                const auto arrived = Clock::now();
                const auto frames = parser.feed(std::string_view(bytes.data(), n));
                std::vector<std::string> packets;
                {
                    std::lock_guard lock(mutex_);
                    if (!state_.connected) break;
                    state_.invalid = parser.invalid_frames();
                    if (frames.empty()) { mark_stale_if_silent(arrived); continue; }
                    for (const auto& frame : frames) packets.push_back(compose(frame, arrived));
                }
                for (const auto& packet : packets) output.write(packet);
                const auto written = Clock::now();
                std::lock_guard lock(mutex_);
                for (std::size_t i = 0; i < packets.size(); ++i) { output_timing_.observe(seconds(written)); ++state_.output_count; }
                const double ms = std::chrono::duration<double, std::milli>(written - arrived).count();
                forward_total_ms_ += ms * packets.size();
                state_.forward_max_ms = std::max(state_.forward_max_ms, ms);
                state_.forward_mean_ms = forward_total_ms_ / state_.output_count;
            }
        } catch (const std::exception& e) { error(e.what()); }
    }
    // One received frame becomes one output packet; called with mutex_ held.
    std::string compose(const Tss1Frame& frame, Clock::time_point arrived) {
        latest_ = frame; freshness_.received(arrived); ++state_.input_count;
        input_timing_.observe(seconds(arrived));
        state_.stale = false;
        double elapsed = std::chrono::duration<double>(arrived - sweep_start_).count();
        if (stop_at_ && elapsed >= *stop_at_) {  // reached CENTER while finishing
            config_.enabled = false; state_.sweep_enabled = false; state_.sweep_finishing = false; stop_at_.reset();
        }
        if (apply_at_ && elapsed >= *apply_at_) {  // reached CENTER: new settings start from CENTER
            config_ = *pending_; sweep_start_ = arrived; elapsed = 0;
            pending_.reset(); apply_at_.reset(); state_.apply_pending = false;
        }
        const double offset = synthetic_roll(config_, elapsed);
        const auto roll = compose_roll(frame.roll_deg, offset);
        if (roll.clipped) ++state_.clipped;
        state_.real = frame.roll_deg; state_.offset = offset; state_.output = roll.roll_deg;
        state_.over_limit = std::abs(roll.roll_deg) > limit_deg_;
        if (state_.over_limit) ++state_.over_limit_count;
        return config_.enabled ? encode_roll(frame, roll.roll_deg) : frame.raw + "\r\n";
    }
    // Called with mutex_ held while no valid frame arrived.
    void mark_stale_if_silent(Clock::time_point now) {
        if (!latest_ || freshness_.fresh(now)) return;
        if (!state_.stale && config_.enabled) state_.sweep_stopped_by_stale = true;
        if (pending_) config_ = *pending_;  // keep the operator's latest settings
        state_.stale = true; config_.enabled = false; state_.sweep_enabled = false;
        state_.sweep_finishing = false; state_.apply_pending = false; stop_at_.reset(); apply_at_.reset(); pending_.reset();
    }
    static LinkTiming timing(const TimingStats& stats, double now) {
        return {stats.current_rate(now), stats.average_rate(), stats.gaps(), stats.intervals()};
    }
    double seconds(Clock::time_point t) const { return std::chrono::duration<double>(t - start_).count(); }
    void error(const char* text) { std::lock_guard lock(mutex_); state_.error = text; state_.connected = false; }
    mutable std::mutex mutex_; SweepConfig config_; ProxySnapshot state_;
    std::optional<Tss1Frame> latest_; std::jthread worker_;
    InputFreshness freshness_;
    double gap_seconds_ = 0.5, forward_total_ms_{};
    // Warning threshold only; the operator sets the downstream MBES's range (NORBIT ±10°,
    // Kongsberg ±15°, R2Sonic about ±(160° − sector)/2). 15° leaves Angle ±10° room for vessel roll.
    double limit_deg_ = 15;
    bool finish_at_center_{};
    std::optional<double> stop_at_;  // sweep time of the CENTER where a finishing Sweep stops
    std::optional<double> apply_at_;  // sweep time of the CENTER where pending_ takes effect
    std::optional<SweepConfig> pending_;
    TimingStats input_timing_, output_timing_;
    Clock::time_point start_ = Clock::now(), sweep_start_ = Clock::now();
};
}
