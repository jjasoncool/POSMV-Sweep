#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace posmv {
struct Tss1Frame {
    std::string raw;
    double heave_m{};
    double roll_deg{};
    double pitch_deg{};
    char status{};
};
class Tss1Parser {
public:
    std::vector<Tss1Frame> feed(std::string_view bytes);
    void finish();
    std::uint64_t invalid_frames() const { return invalid_; }
private:
    std::string buffer_;
    std::uint64_t invalid_{};
};
std::string encode_roll(const Tss1Frame& frame, double roll_deg);
}