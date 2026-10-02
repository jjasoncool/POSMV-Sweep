#include "protocol/tss1.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
namespace posmv {
namespace {
bool digits(std::string_view s) {
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
// The horizontal (XX) and vertical (AAAA) accelerations are hexadecimal, e.g. ":01001D".
bool hex_digits(std::string_view s) {
    for (char c : s) if ((c < '0' || c > '9') && (c < 'A' || c > 'F')) return false;
    return true;
}
double value(std::string_view s) {
    int n = 0;
    for (char c : s.substr(1)) n = n * 10 + (c - '0');
    return (s[0] == '-' ? -n : n) / 100.0;
}
std::optional<Tss1Frame> parse(std::string_view s) {
    if (s.size() != 25 || s[0] != ':' || s[7] != ' ' || s[19] != ' ' ||
        !hex_digits(s.substr(1, 6)) || std::string_view("UuGgHhFf").find(s[13]) == std::string_view::npos)
        return {};
    for (std::size_t p : {8u, 14u, 20u})
        if ((s[p] != ' ' && s[p] != '-') || !digits(s.substr(p + 1, 4))) return {};
    Tss1Frame f{std::string(s), value(s.substr(8, 5)), value(s.substr(14, 5)), value(s.substr(20, 5)), s[13]};
    if (std::abs(f.roll_deg) > 90 || std::abs(f.pitch_deg) > 90) return {};
    return f;
}
}
std::vector<Tss1Frame> Tss1Parser::feed(std::string_view bytes) {
    std::vector<Tss1Frame> result;
    // Byte-wise ingestion keeps storage bounded even for hostile large chunks.
    for (char c : bytes) {
        if (c == ':') {
            if (!buffer_.empty()) ++invalid_;
            buffer_ = ":";
            continue;
        }
        if (buffer_.empty()) continue;
        buffer_ += c;
        if (buffer_.size() >= 2 && buffer_.ends_with("\r\n")) {
            auto f = parse(std::string_view(buffer_).substr(0, buffer_.size() - 2));
            if (f) result.push_back(*f); else ++invalid_;
            buffer_.clear();
        } else if (buffer_.size() >= 27) {
            ++invalid_;
            buffer_.clear();
        }
    }
    return result;
}
void Tss1Parser::finish() {
    if (!buffer_.empty()) { ++invalid_; buffer_.clear(); }
}
std::string encode_roll(const Tss1Frame& frame, double roll_deg) {
    if (!parse(frame.raw) || !std::isfinite(roll_deg) || std::abs(roll_deg) > 90)
        throw std::invalid_argument("Invalid frame or Roll outside +/-90 degrees");
    const auto units = std::lround(roll_deg * 100);
    std::ostringstream field;
    field.imbue(std::locale::classic());
    field << (units < 0 ? '-' : ' ') << std::setw(4) << std::setfill('0') << std::abs(units);
    auto output = frame.raw;
    output.replace(14, 5, field.str());
    return output + "\r\n";
}
}