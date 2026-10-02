#include "app/proxy_engine.hpp"
#include "protocol/tss1.hpp"
#include "simulator/validation.hpp"
#include "memory_pipe.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
using namespace std::chrono_literals;
// The capture's complete frames, found by shape alone (25 characters starting with ':').
std::vector<std::string> complete_frames(const std::string& bytes) {
    std::vector<std::string> frames;
    for (std::size_t start = 0, end; (end = bytes.find("\r\n", start)) != std::string::npos; start = end + 2)
        if (end - start == 25 && bytes[start] == ':') frames.push_back(bytes.substr(start, 25));
    return frames;
}
// A TSS1 angle field (sign or space, then four digits in hundredths) shifted by `hundredths`.
std::string shifted(const std::string& field, int hundredths) {
    const int value = (field[0] == '-' ? -1 : 1) * std::stoi(field.substr(1)) + hundredths;
    char text[8];
    std::snprintf(text, sizeof(text), "%c%04d", value < 0 ? '-' : ' ', std::abs(value));
    return text;
}
std::string forward(const std::string& bytes, const posmv::SweepConfig& config, std::size_t expected_size) {
    auto input = std::make_shared<posmv::test::MemoryPipe>();
    auto output = std::make_shared<posmv::test::MemoryPipe>();
    posmv::ProxyEngine engine;
    engine.configure(config);
    engine.start(input, output);
    input->write(bytes);
    auto forwarded = output->take(expected_size, 5s);
    engine.stop();
    return forwarded;
}
}
int main(int argc, char** argv) {
    {
        const auto frames = posmv::Tss1Parser().feed(":00000A -0355G-0053  0129\r\n:01001D  0012G 0045  0177\r\n");
        check(frames.size() == 2 && std::abs(frames[1].roll_deg - 0.45) < 1e-9,
            "the acceleration fields are hexadecimal, as POS MV sends them");
    }
    if (argc < 2) { std::cerr << "usage: test_capture <capture file>\n"; return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(file), {}};
    {
        posmv::StreamValidator validator;
        validator.feed(bytes);
        validator.finish();
        // 4577 records: a cut-off first line (garbage), 4574 complete frames, one frame with
        // an extra byte at line 682, and a cut-off last frame.
        check(validator.valid == 4574 && validator.invalid == 2 && validator.garbage_bytes == 16,
            "every complete frame of the supplied POS MV capture is valid");
    }
    const auto frames = complete_frames(bytes);
    {
        std::string expected;
        for (const auto& frame : frames) expected += frame + "\r\n";
        posmv::SweepConfig off;
        off.enabled = false;
        check(forward(bytes, off, expected.size()) == expected,
            "with Sweep OFF the Proxy forwards every complete capture frame once, byte for byte");
    }
    {
        posmv::SweepConfig plus5;
        plus5.mode = posmv::SweepMode::Fixed;
        plus5.fixed_offset_deg = 5;
        const auto forwarded = forward(bytes, plus5, frames.size() * 27);
        bool only_roll = forwarded.size() == frames.size() * 27;
        for (std::size_t i = 0; only_roll && i < frames.size(); ++i) {
            const auto out = forwarded.substr(i * 27, 25);
            auto expected = frames[i];
            expected.replace(14, 5, shifted(frames[i].substr(14, 5), 500));
            only_roll = out == expected;
        }
        check(only_roll, "with Sweep ON each capture frame keeps every byte except Roll, which gains the Offset");
    }
    return failures ? 1 : 0;
}
