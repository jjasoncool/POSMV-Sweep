#pragma once
#include <stdexcept>
#include "simulator/validation.hpp"
namespace posmv {
// Resampling permits duplicate frames; never compare TX and RX counts.
class FixedFrameCheck {
public:
    explicit FixedFrameCheck(std::string expected) : expected_(std::move(expected)) {
        StreamValidator check; check.feed(expected_); check.finish();
        if (!check.clean() || check.valid != 1) throw std::invalid_argument("Expected one valid TSS1 frame");
    }
    void feed(std::string_view bytes) {
        for (const auto& frame : validator_.feed(bytes)) {
            if (frame.raw + "\r\n" != expected_) ++mismatches_;
        }
    }
    void finish() { validator_.finish(); }
    bool passed() const { return validator_.clean() && mismatches_ == 0; }
    std::uint64_t mismatches() const { return mismatches_; }
private:
    std::string expected_;
    StreamValidator validator_;
    std::uint64_t mismatches_{};
};
}