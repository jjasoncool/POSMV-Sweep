#include "app/proxy_engine.hpp"
#include "app/input_freshness.hpp"
#include "memory_pipe.hpp"
#include <iostream>
#include <memory>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
using namespace std::chrono_literals;
const std::string frame = ":000011 -0355G-0053  0129\r\n";  // Real Roll -0.53°
struct Link {
    std::shared_ptr<posmv::test::MemoryPipe> input = std::make_shared<posmv::test::MemoryPipe>();
    std::shared_ptr<posmv::test::MemoryPipe> output = std::make_shared<posmv::test::MemoryPipe>();
};
}
int main() {
    {
        posmv::InputFreshness freshness;
        const auto now = posmv::InputFreshness::Clock::now();
        bool ok = !freshness.fresh(now);
        freshness.received(now);
        ok = ok && freshness.fresh(now + 499ms) && !freshness.fresh(now + 500ms);
        freshness.received(now + 1s);
        ok = ok && freshness.fresh(now + 1s);
        freshness.reset();
        check(ok && !freshness.fresh(now), "freshness expires after the Stale Timeout");
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        posmv::SweepConfig off; off.enabled = false;
        engine.configure(off);
        engine.start(link.input, link.output);
        link.input->write(frame + frame + frame);
        check(link.output->take(3 * frame.size(), 1s) == frame + frame + frame,
            "with Sweep OFF every valid frame is forwarded once, byte for byte");
        engine.stop();
    }
    posmv::SweepConfig plus5;  // deterministic Offset: +5° at every moment
    plus5.mode = posmv::SweepMode::Fixed;
    plus5.fixed_offset_deg = 5;
    {
        Link link;
        posmv::ProxyEngine engine;
        engine.configure(plus5);
        engine.start(link.input, link.output);
        link.input->write(frame);
        check(link.output->take(frame.size(), 1s) == ":000011 -0355G 0447  0129\r\n",
            "with Sweep ON only the Roll field changes (-0.53° + 5° = 4.47°)");
        engine.stop();
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        posmv::SweepConfig off; off.enabled = false;
        engine.configure(off);
        engine.start(link.input, link.output);
        check(engine.snapshot().state == posmv::ProxyState::Waiting, "connected without input is Waiting");
        link.input->write(":000011 -0355X-0053  0129\r\n" + frame);  // unknown status flag
        const auto forwarded = link.output->take(frame.size(), 1s);
        std::this_thread::sleep_for(100ms);
        check(forwarded == frame && engine.snapshot().invalid == 1, "invalid frames are dropped and counted");
        engine.stop();
        check(engine.snapshot().state == posmv::ProxyState::Disconnected, "stopping disconnects");
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        engine.stale_timeout(150ms);
        engine.configure(plus5);
        engine.start(link.input, link.output);
        link.input->write(frame);
        link.output->take(frame.size(), 1s);
        std::this_thread::sleep_for(400ms);  // input silent beyond the Stale Timeout
        const auto stale = engine.snapshot();
        link.input->write(frame);
        const auto resumed = link.output->take(frame.size(), 1s);
        std::this_thread::sleep_for(100ms);
        const auto after = engine.snapshot();
        check(stale.state == posmv::ProxyState::Stale && !stale.sweep_enabled && stale.sweep_stopped_by_stale &&
            resumed == frame && after.state == posmv::ProxyState::Active && !after.sweep_enabled,
            "stale input switches Sweep OFF and resumed frames pass through unchanged");
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        engine.stabilisation_limit(4.0);  // MBES can only compensate ±4°
        engine.configure(plus5);
        engine.start(link.input, link.output);
        link.input->write(frame);
        link.output->take(frame.size(), 1s);
        std::this_thread::sleep_for(50ms);
        const auto beyond = engine.snapshot();
        engine.enable(false);
        link.input->write(frame);
        link.output->take(frame.size(), 1s);
        std::this_thread::sleep_for(50ms);
        const auto within = engine.snapshot();
        check(beyond.over_limit && beyond.over_limit_count == 1 && !within.over_limit && within.over_limit_count == 1,
            "output Roll beyond the MBES stabilisation limit is flagged and counted");
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        posmv::SweepConfig fast;  // CENTER every 0.2 s
        fast.cycle_seconds = 0.4;
        engine.configure(fast);
        engine.finish_at_center(true);
        engine.start(link.input, link.output);
        std::jthread feeder([&](std::stop_token token) {
            while (!token.stop_requested()) { link.input->write(frame); std::this_thread::sleep_for(10ms); }
        });
        std::this_thread::sleep_for(250ms);
        engine.enable(false);
        std::this_thread::sleep_for(20ms);
        const auto finishing = engine.snapshot();
        std::this_thread::sleep_for(300ms);  // longer than half a Cycle
        const auto stopped = engine.snapshot();
        link.output->take(0, 0ms);
        const auto next = link.output->take(frame.size(), 1s);
        feeder.request_stop();
        check(finishing.sweep_finishing && finishing.sweep_enabled && !stopped.sweep_finishing && !stopped.sweep_enabled &&
            next.starts_with(frame), "finishing at CENTER keeps the sine running until the next CENTER, then stops");
    }
    {
        Link link;
        posmv::ProxyEngine engine;
        posmv::SweepConfig fast;  // ±10°, CENTER every 0.2 s
        fast.cycle_seconds = 0.4;
        engine.configure(fast);
        engine.finish_at_center(true);
        engine.start(link.input, link.output);
        std::jthread feeder([&](std::stop_token token) {
            while (!token.stop_requested()) { link.input->write(frame); std::this_thread::sleep_for(10ms); }
        });
        std::this_thread::sleep_for(250ms);
        auto small = fast;
        small.amplitude_deg = 2;
        engine.configure(small);
        const auto pending = engine.snapshot();
        std::this_thread::sleep_for(250ms);  // past the next CENTER
        double largest = 0;
        for (int i = 0; i < 20; ++i) {
            largest = std::max(largest, std::abs(engine.snapshot().offset));
            std::this_thread::sleep_for(10ms);
        }
        const auto applied = engine.snapshot();
        feeder.request_stop();
        check(pending.apply_pending && !applied.apply_pending && applied.sweep_enabled && largest <= 2.0 + 1e-9,
            "Apply waits for the next CENTER when finishing at CENTER is chosen");
    }
    return failures ? 1 : 0;
}
