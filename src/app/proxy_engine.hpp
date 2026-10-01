#pragma once
#include "motion/sweep.hpp"
#include "protocol/tss1.hpp"
#include "transport/serial_transport.hpp"
#include "scheduler/output_scheduler.hpp"
#include <mutex>
#include <thread>
#include <optional>
#include <array>
#include "app/input_freshness.hpp"
namespace posmv {
struct ProxySnapshot {
    bool connected{};
    bool stale{}, sweep_enabled{};
    double real{}, offset{}, output{}, input_rate{}, output_rate{}, lateness_ms{};
    double average_ms{}, minimum_ms{}, maximum_ms{};
    std::uint64_t input_count{}, output_count{}, invalid{}, late{}, clipped{};
    std::string status = "Disconnected";
};
class ProxyEngine {
public:
    ~ProxyEngine() { stop(); }
    void configure(SweepConfig config) {
        config.validate(); std::lock_guard lock(mutex_);
        if (config.enabled && state_.stale) throw std::runtime_error("Input stale: cannot enable sweep");
        config_ = config; sweep_start_ = std::chrono::steady_clock::now();
        state_.sweep_enabled = config.enabled;
    }
    void enable(bool enabled) {
        std::lock_guard lock(mutex_);
        if (enabled && state_.stale) throw std::runtime_error("Input stale: cannot enable sweep");
        if (enabled && !config_.enabled) sweep_start_ = std::chrono::steady_clock::now();
        config_.enabled = enabled; state_.sweep_enabled = enabled;
    }
    void stale_timeout(std::chrono::milliseconds value) { std::lock_guard lock(mutex_); freshness_.timeout(value); }
    ProxySnapshot snapshot() const { std::lock_guard lock(mutex_); return state_; }
    void stop() {
        receiver_.request_stop(); sender_.request_stop();
        if (receiver_.joinable()) receiver_.join();
        if (sender_.joinable()) sender_.join();
        std::lock_guard lock(mutex_); state_.connected = false;
        if (!state_.status.starts_with("Error:")) state_.status = "Disconnected";
    }
    void start(const std::string& input, const std::string& output, unsigned baud, unsigned rate, bool simulation) {
        if (rate != 25 && rate != 50 && rate != 100) throw std::invalid_argument("Invalid rate");
        if (!simulation && input == output) throw std::invalid_argument("Input and output must differ");
        if (!simulation) validate_serial_throughput(baud, rate);
        stop();
        std::shared_ptr<SerialTransport> rx, tx;
        if (!simulation) { rx = std::make_shared<SerialTransport>(input, baud); tx = std::make_shared<SerialTransport>(output, baud); }
        { std::lock_guard lock(mutex_); state_ = {}; state_.connected = true; latest_.reset(); freshness_.reset(); sweep_start_ = std::chrono::steady_clock::now(); state_.sweep_enabled = config_.enabled; state_.status = simulation ? "Simulation — no hardware output" : "Connected"; }
        receiver_ = std::jthread([this, rx, rate, simulation](std::stop_token token) {
            try {
                Tss1Parser parser; OutputScheduler pacing(rate); std::array<char, 4096> bytes{};
                const auto start = std::chrono::steady_clock::now();
                while (!token.stop_requested()) {
                    std::vector<Tss1Frame> frames;
                    if (simulation) { pacing.wait(); frames = parser.feed(":000011 -0355G 0200  0129\r\n"); }
                    else { const auto n = rx->read(bytes); frames = parser.feed(std::string_view(bytes.data(), n)); }
                    std::lock_guard lock(mutex_);
                    if (!state_.connected) break;
                    for (auto& f : frames) { latest_ = f; freshness_.received(std::chrono::steady_clock::now()); ++state_.input_count; }
                    state_.invalid = parser.invalid_frames();
                    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    if (elapsed > 0) state_.input_rate = state_.input_count / elapsed;
                }
            } catch (const std::exception& e) { error(e.what()); }
        });
        sender_ = std::jthread([this, tx, rate, simulation](std::stop_token token) {
            try {
                OutputScheduler pacing(rate);
                auto previous = std::chrono::steady_clock::time_point{};
                double interval_sum = 0;
                while (!token.stop_requested()) {
                    const double elapsed = pacing.wait(); Tss1Frame frame; SweepConfig config;
                    double sweep_elapsed;
                    {
                        std::lock_guard lock(mutex_);
                        if (!state_.connected) break;
                        if (!latest_) continue;
                        if (!freshness_.fresh(std::chrono::steady_clock::now())) {
                            state_.stale = true; config_.enabled = false; state_.sweep_enabled = false;
                            state_.status = "Input stale — output suspended"; state_.output_rate = 0;
                            continue;
                        }
                        if (state_.stale) { state_.stale = false; state_.status = "Input recovered — Sweep OFF"; }
                        frame = *latest_; config = config_;
                        sweep_elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - sweep_start_).count();
                    }
                    const double offset = synthetic_roll(config, sweep_elapsed); const auto roll = compose_roll(frame.roll_deg, offset);
                    const auto packet = config.enabled ? encode_roll(frame, roll.roll_deg) : frame.raw + "\r\n";
                    if (!simulation) tx->write(packet);
                    const auto now = std::chrono::steady_clock::now();
                    std::lock_guard lock(mutex_);
                    if (previous != std::chrono::steady_clock::time_point{}) {
                        const double interval = std::chrono::duration<double, std::milli>(now - previous).count();
                        interval_sum += interval;
                        state_.minimum_ms = state_.output_count == 1 ? interval : std::min(state_.minimum_ms, interval);
                        state_.maximum_ms = std::max(state_.maximum_ms, interval);
                        state_.average_ms = interval_sum / state_.output_count;
                    }
                    previous = now;
                    ++state_.output_count;
                    state_.real = frame.roll_deg; state_.offset = offset; state_.output = roll.roll_deg;
                    if (roll.clipped) ++state_.clipped;
                    if (elapsed > 0) state_.output_rate = state_.output_count / elapsed;
                    state_.late = pacing.late_frames; state_.lateness_ms = pacing.max_lateness_ms;
                }
            } catch (const std::exception& e) { error(e.what()); }
        });
    }
private:
    void error(const char* text) { std::lock_guard lock(mutex_); state_.status = "Error: " + std::string(text); state_.connected = false; }
    mutable std::mutex mutex_; SweepConfig config_; ProxySnapshot state_;
    std::optional<Tss1Frame> latest_; std::jthread receiver_, sender_;
    InputFreshness freshness_;
    std::chrono::steady_clock::time_point sweep_start_ = std::chrono::steady_clock::now();
};
}