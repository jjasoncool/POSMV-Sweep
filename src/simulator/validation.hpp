#pragma once
#include "protocol/tss1.hpp"
#include <string>
#include <vector>
namespace posmv {
// Unlike the recovery parser, the validator reports every byte outside valid frames.
class StreamValidator {
public:
    std::vector<Tss1Frame> feed(std::string_view bytes) {
        std::vector<Tss1Frame> frames;
        for (char c : bytes) {
            if (c == ':') {
                if (!candidate_.empty()) { ++invalid; last_error = "Interrupted frame"; }
                candidate_ = ":";
                continue;
            }
            if (candidate_.empty()) { ++garbage_bytes; last_error = "Bytes outside a frame"; continue; }
            candidate_ += c;
            if (candidate_.ends_with("\r\n") || candidate_.size() >= 27) {
                Tss1Parser parser;
                auto parsed = parser.feed(candidate_);
                if (parsed.size() == 1 && candidate_.size() == 27) {
                    ++valid;
                    frames.push_back(parsed.front());
                } else { ++invalid; last_error = "Invalid length, CRLF, field, status or range"; }
                candidate_.clear();
            }
        }
        return frames;
    }
    void finish() {
        if (!candidate_.empty()) { ++invalid; last_error = "Truncated final frame"; candidate_.clear(); }
    }
    bool clean() const { return valid > 0 && invalid == 0 && garbage_bytes == 0; }
    std::uint64_t valid{}, invalid{}, garbage_bytes{};
    std::string last_error;
private:
    std::string candidate_;
};
}