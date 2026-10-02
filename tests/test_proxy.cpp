#include "app/proxy_engine.hpp"
#include <iostream>
#include "app/input_freshness.hpp"
int main() {
    posmv::InputFreshness freshness;
    const auto now = posmv::InputFreshness::Clock::now();
    if (freshness.fresh(now)) return 1;
    freshness.received(now);
    if (!freshness.fresh(now + std::chrono::milliseconds(499)) ||
        freshness.fresh(now + std::chrono::milliseconds(500))) return 1;
    freshness.received(now + std::chrono::seconds(1));
    if (!freshness.fresh(now + std::chrono::seconds(1))) return 1;
    freshness.reset(); if (freshness.fresh(now)) return 1;
    posmv::ProxyEngine engine;
    posmv::SweepConfig config; config.enabled = false;
    engine.configure(config);
    for (unsigned rate : {25u, 50u, 100u}) {
        engine.start("COM11", "COM12", 38400, rate, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        const auto state = engine.snapshot();
        if (!state.connected || state.input_count < 2 || state.output_count < 2 ||
            state.invalid || state.real != 2 || state.output != 2 || state.offset != 0) {
            std::cerr << "Simulation/pass-through failed\n"; return 1;
        }
        if (state.state != posmv::ProxyState::Active || state.output_timing.current_hz <= 0) {
            std::cerr << "Flowing input should report Active with a current output rate\n"; return 1;
        }
        engine.stop();
        if (engine.snapshot().connected || engine.snapshot().state != posmv::ProxyState::Disconnected) return 1;
    }
    config.enabled = true; engine.configure(config);
    engine.start("COM11", "COM12", 38400, 100, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto state = engine.snapshot();
    if (state.offset >= 0 || std::abs(state.output - state.real - state.offset) > 0.001) return 1;
    engine.stop();
}