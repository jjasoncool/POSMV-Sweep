#include "protocol/tss1.hpp"
#include "simulator/validation.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
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
    posmv::StreamValidator validator;
    validator.feed(bytes);
    validator.finish();
    // 4577 records: a cut-off first line (garbage), 4574 complete frames, one frame with
    // an extra byte at line 682, and a cut-off last frame.
    check(validator.valid == 4574 && validator.invalid == 2 && validator.garbage_bytes == 16,
        "every complete frame of the supplied POS MV capture is valid");
    return failures ? 1 : 0;
}
