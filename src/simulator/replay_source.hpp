#pragma once
#include "simulator/validation.hpp"
#include <array>
#include <istream>
#include <stop_token>
#include <stdexcept>
namespace posmv {
struct ReplaySource {
    std::vector<Tss1Frame> frames;
    std::uint64_t invalid{}, garbage{};
};
inline ReplaySource read_replay(std::istream& stream, bool strict, std::stop_token stop = {}) {
    StreamValidator validator;
    ReplaySource result;
    std::array<char, 4096> bytes{};
    while (true) {
        if (stop.stop_requested()) throw std::runtime_error("Replay cancelled");
        stream.read(bytes.data(), bytes.size());
        auto frames = validator.feed(std::string_view(bytes.data(), stream.gcount()));
        result.frames.insert(result.frames.end(), frames.begin(), frames.end());
        if (stream.bad()) throw std::runtime_error("Replay read failed");
        if (stream.eof()) break;
        if (stream.fail()) throw std::runtime_error("Replay stream failed");
    }
    validator.finish();
    result.invalid = validator.invalid; result.garbage = validator.garbage_bytes;
    if (result.frames.empty()) throw std::runtime_error("Replay contains no valid frames");
    if (strict && !validator.clean()) throw std::runtime_error("Strict replay rejected malformed or extra bytes");
    return result;
}
}