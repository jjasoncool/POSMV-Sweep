#pragma once
#include "motion/sweep.hpp"
#include "protocol/tss1.hpp"
#include "transport/serial_transport.hpp"
#include "scheduler/output_scheduler.hpp"
#include "diagnostics/timing_stats.hpp"
#include "app/input_freshness.hpp"
#include <mutex>
#include <thread>
#include <optional>
#include <array>
#include <string>
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
    double real{}, offset{}, output{}, lateness_ms{};
    LinkTiming input_timing, output_timing;
    std::uint64_t input_count{}, output_count{}, invalid{}, late{}, clipped{};
};
class ProxyEngine {
public:
    ~ProxyEngine() { stop(); }
    void configure(SweepConfig config) {
        config.validate(); std::lock_guard lock(mutex_);
        if (config.enabled && state_.stale) throw std::runtime_error("資料過期中，無法開啟 Sweep");
        config_ = config; sweep_start_ = std::chrono::steady_clock::now();
        state_.sweep_enabled = config.enabled;
        if (config.enabled) state_.sweep_stopped_by_stale = false;
    }
    void enable(bool enabled) {
        std::lock_guard lock(mutex_);
        if (enabled && state_.stale) throw std::runtime_error("資料過期中，無法開啟 Sweep");
        if (enabled && !config_.enabled) sweep_start_ = std::chrono::steady_clock::now();
        config_.enabled = enabled; state_.sweep_enabled = enabled;
        if (enabled) state_.sweep_stopped_by_stale = false;
    }
    void stale_timeout(std::chrono::milliseconds value) {
        std::lock_guard lock(mutex_); freshness_.timeout(value); gap_seconds_ = value.count() / 1000.0;
    }
    ProxySnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        auto s = state_;
        const double now = seconds_since_start();
        s.input_timing = timing(input_timing_, now);
        s.output_timing = timing(output_timing_, now);
        s.state = !s.error.empty() ? ProxyState::Error : !s.connected ? ProxyState::Disconnected
            : s.stale ? ProxyState::Stale : !latest_ ? ProxyState::Waiting : ProxyState::Active;
        return s;
    }
    void stop() {
        receiver_.request_stop(); sender_.request_stop();
        if (receiver_.joinable()) receiver_.join();
        if (sender_.joinable()) sender_.join();
        std::lock_guard lock(mutex_); state_.connected = false;
    }
    void start(const std::string& input, const std::string& output, unsigned baud, unsigned rate, bool simulation) {
        if (rate != 25 && rate != 50 && rate != 100) throw std::invalid_argument("Invalid rate");
        if (!simulation && input == output) throw std::invalid_argument("輸入與輸出 COM 不能相同");
        if (!simulation) validate_serial_throughput(baud, rate);
        stop();
        std::shared_ptr<SerialTransport> rx, tx;
        if (!simulation) { rx = std::make_shared<SerialTransport>(input, baud); tx = std::make_shared<SerialTransport>(output, baud); }
        {
            std::lock_guard lock(mutex_);
            state_ = {}; state_.connected = true; latest_.reset(); freshness_.reset();
            start_ = sweep_start_ = std::chrono::steady_clock::now(); state_.sweep_enabled = config_.enabled;
            // A pause longer than the Stale Timeout is a gap, not an interval.
            input_timing_ = TimingStats(gap_seconds_); output_timing_ = TimingStats(gap_seconds_);
        }
        receiver_ = std::jthread([this, rx, rate, simulation](std::stop_token token) {
            try {
                Tss1Parser parser; OutputScheduler pacing(rate); std::array<char, 4096> bytes{};
                while (!token.stop_requested()) {
                    std::vector<Tss1Frame> frames;
                    if (simulation) { pacing.wait(); frames = parser.feed(":000011 -0355G 0200  0129\r\n"); }
                    else { const auto n = rx->read(bytes); frames = parser.feed(std::string_view(bytes.data(), n)); }
                    std::lock_guard lock(mutex_);
                    if (!state_.connected) break;
                    for (auto& f : frames) {
                        latest_ = f; freshness_.received(std::chrono::steady_clock::now()); ++state_.input_count;
                        input_timing_.observe(seconds_since_start());
                    }
                    state_.invalid = parser.invalid_frames();
                }
            } catch (const std::exception& e) { error(e.what()); }
        });
        sender_ = std::jthread([this, tx, rate, simulation](std::stop_token token) {
            try {
                OutputScheduler pacing(rate);
                while (!token.stop_requested()) {
                    pacing.wait(); Tss1Frame frame; SweepConfig config;
                    double sweep_elapsed;
                    {
                        std::lock_guard lock(mutex_);
                        if (!state_.connected) break;
                        if (!latest_) continue;
                        if (!freshness_.fresh(std::chrono::steady_clock::now())) {
                            if (!state_.stale && config_.enabled) state_.sweep_stopped_by_stale = true;
                            state_.stale = true; config_.enabled = false; state_.sweep_enabled = false;
                            continue;
                        }
                        state_.stale = false;
                        frame = *latest_; config = config_;
                        sweep_elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - sweep_start_).count();
                    }
                    const double offset = synthetic_roll(config, sweep_elapsed); const auto roll = compose_roll(frame.roll_deg, offset);
                    const auto packet = config.enabled ? encode_roll(frame, roll.roll_deg) : frame.raw + "\r\n";
                    if (!simulation) tx->write(packet);
                    std::lock_guard lock(mutex_);
                    output_timing_.observe(seconds_since_start());
                    ++state_.output_count;
                    state_.real = frame.roll_deg; state_.offset = offset; state_.output = roll.roll_deg;
                    if (roll.clipped) ++state_.clipped;
                    state_.late = pacing.late_frames; state_.lateness_ms = pacing.max_lateness_ms;
                }
            } catch (const std::exception& e) { error(e.what()); }
        });
    }
private:
    static LinkTiming timing(const TimingStats& stats, double now) {
        return {stats.current_rate(now), stats.average_rate(), stats.gaps(), stats.intervals()};
    }
    double seconds_since_start() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(); }
    void error(const char* text) { std::lock_guard lock(mutex_); state_.error = text; state_.connected = false; }
    mutable std::mutex mutex_; SweepConfig config_; ProxySnapshot state_;
    std::optional<Tss1Frame> latest_; std::jthread receiver_, sender_;
    InputFreshness freshness_;
    double gap_seconds_ = 0.5;
    TimingStats input_timing_, output_timing_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point sweep_start_ = std::chrono::steady_clock::now();
};
}
