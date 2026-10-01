#include <stdexcept>
#include "simulator/fixed_frame_check.hpp"
int main() {
    const std::string frame = ":000000  0000G 0000  0000\r\n";
    posmv::FixedFrameCheck check(frame);
    if (check.passed()) return 1;
    check.feed(frame.substr(0, 9)); check.feed(frame.substr(9));
    check.feed(frame); check.finish();
    if (!check.passed()) return 2;
    check.feed(":000000  0000G 0001  0000\r\n");
    if (check.passed() || check.mismatches() != 1) return 3;
    posmv::FixedFrameCheck garbage(frame); garbage.feed(frame + "x");
    if (garbage.passed()) return 4;
    posmv::FixedFrameCheck truncated(frame); truncated.feed(frame + ":partial"); truncated.finish();
    if (truncated.passed()) return 5;
    try { posmv::FixedFrameCheck invalid("bad"); return 6; } catch(const std::invalid_argument&) {}
}