#include "protocol/tss1.hpp"
#include "motion/sweep.hpp"
#include "simulator/validation.hpp"
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
void check(bool value) { if (!value) throw std::runtime_error("check failed"); }
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        std::ofstream file(argv[2], std::ios::binary);
        file << ":000011 -0355G-0053  0129\r\n:000011 -0355G-0053  0129\r\n";
        return file ? 0 : 1;
    }
    try {
        posmv::Tss1Parser parser;
        auto frames = parser.feed(":000011 -0355G-0053  0129\r\n");
        check(frames.size() == 1);
        check(std::abs(frames[0].roll_deg + 0.53) < 1e-9);
        check(posmv::encode_roll(frames[0], 1.29) == ":000011 -0355G 0129  0129\r\n");
        const std::string record = ":000011 -0355G-0053  0129\r\n";
        posmv::StreamValidator validator;
        validator.feed(record);
        validator.finish();
        check(validator.clean());
        validator.feed("garbage");
        check(!validator.clean() && validator.garbage_bytes == 7);
        for (std::size_t split = 0; split <= record.size(); ++split) {
            posmv::Tss1Parser chunks;
            auto first = chunks.feed(record.substr(0, split));
            auto second = chunks.feed(record.substr(split));
            check(first.size() + second.size() == 1);
        }
        posmv::Tss1Parser recovery;
        check(recovery.feed("garbage:bad\r\n:partial" + record + record).size() == 2);
        check(recovery.invalid_frames() == 2);
        check(recovery.feed(":truncated").empty());
        recovery.finish();
        check(recovery.invalid_frames() == 3);
        for (double roll : {-90.0, -1.29, 0.0, 1.29, 90.0}) {
            posmv::Tss1Parser roundtrip;
            auto encoded = posmv::encode_roll(frames[0], roll);
            check(encoded.size() == 27);
            auto decoded = roundtrip.feed(encoded);
            check(decoded.size() == 1 && std::abs(decoded[0].roll_deg - roll) < 1e-9);
            check(decoded[0].heave_m == -3.55 && decoded[0].pitch_deg == 1.29);
        }
        posmv::SweepConfig config;
        check(std::abs(posmv::synthetic_roll(config, 0)) < 1e-9);
        check(std::abs(posmv::synthetic_roll(config, 1) + 10) < 1e-9);
        check(std::abs(posmv::synthetic_roll(config, 2)) < 1e-9);
        check(std::abs(posmv::synthetic_roll(config, 3) - 10) < 1e-9);
        check(std::abs(posmv::synthetic_roll(config, 4)) < 1e-9);
        check(posmv::compose_roll(2, -8).roll_deg == -6);
        check(posmv::compose_roll(89, 10).clipped);
        config.enabled = false;
        check(posmv::synthetic_roll(config, 1) == 0);
        check(posmv::compose_roll(2, posmv::synthetic_roll(config, 1)).roll_deg == 2);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}