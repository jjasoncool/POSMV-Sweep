#include "protocol/tss1.hpp"
#include "motion/sweep.hpp"
#include "scheduler/output_scheduler.hpp"
#include "transport/serial_transport.hpp"
#include "simulator/validation.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
namespace {
double number(const std::string& text) {
    std::size_t used{};
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::invalid_argument("Invalid number: " + text);
    return value;
}
unsigned integer(const std::string& text) {
    const double value = number(text);
    if (value < 0 || value > std::numeric_limits<unsigned>::max() || std::floor(value) != value)
        throw std::invalid_argument("Expected unsigned integer: " + text);
    return static_cast<unsigned>(value);
}
}
#ifdef POSMV_HAS_GUI
int launch_simulator_gui();
#endif
int main(int argc, char** argv) {
#ifdef POSMV_HAS_GUI
    if (argc == 1) return launch_simulator_gui();
#endif
    try {
        if (argc > 1 && std::string(argv[1]) == "validate") {
            if (argc != 4 || std::string(argv[2]) != "--input")
                throw std::invalid_argument("Usage: posmv-simulator validate --input FILE");
            std::ifstream input(argv[3], std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open validation input");
            posmv::StreamValidator validator;
            std::array<char, 4096> bytes{};
            while (input.read(bytes.data(), bytes.size()) || input.gcount() > 0)
                validator.feed(std::string_view(bytes.data(), input.gcount()));
            if (input.bad()) throw std::runtime_error("Validation read failed");
            validator.finish();
            std::cout << "Validation: valid=" << validator.valid << " invalid=" << validator.invalid
                << " garbage_bytes=" << validator.garbage_bytes << '\n';
            if (!validator.clean()) std::cerr << validator.last_error << '\n';
            return validator.clean() ? 0 : 2;
        }
        std::string port, output, replay;
        unsigned baud = 38400, rate = 25, count = 100;
        bool sine = false, unpaced = false, loop = false;
        double fixed_roll = 0;
        posmv::SweepConfig sweep;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (i == 1 && arg == "send") continue;
            if (arg == "--help") {
                std::cout << "posmv-simulator (--port COM10 | --output FILE) [--baud 38400]\n"
                    "[--rate 25|50|100] [--count 100] [--replay FILE]\n"
                    "[--roll 0] [--sine-roll] [--angle 10] [--cycle 4] [--unpaced]\n"
                    "Default: 100 zero-motion frames, 8N1, no flow control. Replay once to EOF.\n"
                    "--count limits replay if supplied. --unpaced is file-only.\n";
                std::cout << "--loop repeats replay until --count (default 100 for loop).\n"
                    "validate --input FILE: strict byte-stream validation; exit 2 if invalid.\n";
                return 0;
            }
            if (arg == "--sine-roll") { sine = true; continue; }
            if (arg == "--unpaced") { unpaced = true; continue; }
            if (arg == "--loop") { loop = true; continue; }
            if (++i >= argc) throw std::invalid_argument("Missing value: " + arg);
            const std::string value = argv[i];
            if (arg == "--port") port = value;
            else if (arg == "--output") output = value;
            else if (arg == "--replay") replay = value;
            else if (arg == "--baud") baud = integer(value);
            else if (arg == "--rate") rate = integer(value);
            else if (arg == "--count") count = integer(value);
            else if (arg == "--roll") fixed_roll = number(value);
            else if (arg == "--angle") sweep.amplitude_deg = number(value);
            else if (arg == "--cycle") sweep.cycle_seconds = number(value);
            else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (port.empty() == output.empty()) throw std::invalid_argument("Select exactly one --port or --output");
        if (rate != 25 && rate != 50 && rate != 100) throw std::invalid_argument("Rate must be 25, 50 or 100");
        if (count == 0 || std::abs(fixed_roll) > 90) throw std::invalid_argument("Count must be positive; Roll must be +/-90");
        sweep.validate();
        if (loop && replay.empty()) throw std::invalid_argument("--loop requires --replay");
        if (!replay.empty() && (sine || fixed_roll != 0)) throw std::invalid_argument("Replay cannot combine with generated Roll");
        if (!port.empty() && unpaced) throw std::invalid_argument("Serial output must be paced");
        std::ifstream source;
        if (!replay.empty()) {
            if (!output.empty() && (std::filesystem::weakly_canonical(replay) == std::filesystem::weakly_canonical(output) ||
                (std::filesystem::exists(output) && std::filesystem::equivalent(replay, output))))
                throw std::invalid_argument("Replay input and output must differ");
            source.open(replay, std::ios::binary);
            if (!source) throw std::runtime_error("Cannot open replay input");
            bool count_supplied = false;
            for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--count") count_supplied = true;
            if (!count_supplied && !loop) count = std::numeric_limits<unsigned>::max();
        }
        std::unique_ptr<posmv::SerialTransport> serial;
        std::ofstream file;
        if (!port.empty()) {
            posmv::validate_serial_throughput(baud, rate);
            serial = std::make_unique<posmv::SerialTransport>(port, baud);
        } else {
            file.open(output, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("Cannot open output");
        }
        posmv::Tss1Parser parser;
        posmv::OutputScheduler scheduler(rate);
        const posmv::Tss1Frame generated{":000000  0000G 0000  0000", 0, 0, 0, 'G'};
        unsigned emitted{};
        auto send = [&](const posmv::Tss1Frame& frame) {
            const double elapsed = unpaced ? static_cast<double>(emitted) / rate : scheduler.wait();
            const std::string packet = replay.empty() ? posmv::encode_roll(frame, sine ?
                posmv::synthetic_roll(sweep, elapsed) : fixed_roll) : frame.raw + "\r\n";
            if (serial) serial->write(packet);
            else { file << packet; if (!file) throw std::runtime_error("File write failed"); }
            ++emitted;
        };
        if (replay.empty()) {
            while (emitted < count) send(generated);
        } else {
            std::array<char, 4096> bytes{};
            do {
            const unsigned before = emitted;
            while (emitted < count && (source.read(bytes.data(), bytes.size()) || source.gcount() > 0)) {
                for (const auto& frame : parser.feed(std::string_view(bytes.data(), source.gcount()))) {
                    if (emitted == count) break;
                    send(frame);
                }
            }
            if (source.bad()) throw std::runtime_error("Replay read failed");
            if (source.eof()) parser.finish();
            if (emitted == before) throw std::runtime_error("Replay contains no valid frames");
            if (!loop || emitted >= count) break;
            source.clear();
            source.seekg(0);
            if (!source) throw std::runtime_error("Cannot rewind replay");
            } while (emitted < count);
        }
        if (file.is_open()) { file.flush(); if (!file) throw std::runtime_error("Output flush failed"); }
        std::cerr << "Simulator: output=" << emitted << " invalid=" << parser.invalid_frames()
            << " late(>1ms)=" << scheduler.late_frames << " max_lateness_ms=" << scheduler.max_lateness_ms << '\n';
        if (emitted == 0) throw std::runtime_error("No valid replay frames");
        return 0;
    } catch (const std::exception& e) { std::cerr << "Simulator: " << e.what() << '\n'; return 1; }
}