#include "simulator/replay_source.hpp"
#include <sstream>
#include <iostream>
int main() {
    const std::string frame = ":000011 -0355G-0053  0129\r\n";
    auto rejects = [](auto&& fn) { try { fn(); } catch (const std::runtime_error&) { return true; } return false; };
    std::istringstream valid(frame + frame);
    auto clean = posmv::read_replay(valid, true);
    if (clean.frames.size() != 2 || clean.invalid || clean.garbage) return 1;
    std::istringstream dirty("xx" + frame + ":partial");
    auto tolerant = posmv::read_replay(dirty, false);
    if (tolerant.frames.size() != 1 || tolerant.invalid != 1 || tolerant.garbage != 2) return 2;
    std::istringstream strict("xx" + frame);
    if (!rejects([&] { posmv::read_replay(strict, true); })) return 3;
    std::istringstream empty;
    if (!rejects([&] { posmv::read_replay(empty, false); })) return 4;
    std::stop_source stop; stop.request_stop(); std::istringstream cancelled(frame);
    if (!rejects([&] { posmv::read_replay(cancelled, false, stop.get_token()); })) return 5;
    std::cout << "Replay preflight cases passed\n";
}