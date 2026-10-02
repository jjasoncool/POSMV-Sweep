#include "protocol/tss1.hpp"
#include "app/console.hpp"
#include "motion/sweep.hpp"
#include "scheduler/output_scheduler.hpp"
#include "transport/serial_transport.hpp"
#include <chrono>
#include <array>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef POSMV_HAS_GUI
int launch_gui();
#endif
namespace {
double number(const std::string& s) {
    std::size_t used{};
    double value = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(value)) throw std::invalid_argument("Invalid number: " + s);
    return value;
}
}
int main(int argc, char** argv) {
    posmv::setup_console(argc, argv);
    try {
#ifdef POSMV_HAS_GUI
        if (argc == 1) return launch_gui();
#endif
        std::string input, output;
        std::string input_port;
        unsigned baud = 38400;
        double seconds = 10;
        posmv::SweepConfig config;
        double rate = 25;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "POSMV-Sweep\n--replay INPUT --output OUTPUT [--angle 10] [--cycle 4]\n"
                             "[--rate 25|50|100|200] [--off] [--invert]\n"
                             "Serial capture: --input-port COM11 --output FILE [--baud 38400] [--seconds 10]\n"
                             "Offline timestamps assume one input frame per selected rate.\n";
                return 0;
            }
            if (arg == "--off") { config.enabled = false; continue; }
            if (arg == "--invert") { config.direction = posmv::SweepDirectionConvention::PositiveFirst; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--replay") input = value;
            else if (arg == "--input-port") input_port = value;
            else if (arg == "--seconds") seconds = number(value);
            else if (arg == "--baud") {
                const double n = number(value);
                if (n <= 0 || n > 4000000 || std::floor(n) != n) throw std::invalid_argument("Invalid baud");
                baud = static_cast<unsigned>(n);
            }
            else if (arg == "--output") output = value;
            else if (arg == "--angle") config.amplitude_deg = number(value);
            else if (arg == "--cycle") config.cycle_seconds = number(value);
            else if (arg == "--rate") rate = number(value);
            else throw std::invalid_argument("Unknown option: " + arg);
        }
        config.validate();
        if (!posmv::supported_rate(rate)) throw std::invalid_argument("Rate must be 25, 50, 100 or 200 Hz");
        if (!input_port.empty()) {
            if (!input.empty() || output.empty() || seconds <= 0)
                throw std::invalid_argument("Capture requires --output, positive --seconds, no --replay");
            posmv::validate_serial_throughput(baud, static_cast<unsigned>(rate));
            posmv::SerialTransport serial(input_port, baud);
            std::ofstream destination(output, std::ios::binary | std::ios::trunc);
            if (!destination) throw std::runtime_error("Cannot open capture output");
            posmv::Tss1Parser parser;
            std::array<char, 4096> bytes{};
            const auto start = std::chrono::steady_clock::now();
            std::uint64_t count{}, clipped{};
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
                const auto received = serial.read(bytes);
                for (const auto& frame : parser.feed(std::string_view(bytes.data(), received))) {
                    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    const auto roll = posmv::compose_roll(frame.roll_deg, posmv::synthetic_roll(config, elapsed));
                    if (roll.clipped) ++clipped;
                    destination << (config.enabled ? posmv::encode_roll(frame, roll.roll_deg) : frame.raw + "\r\n");
                    if (!destination) throw std::runtime_error("Capture write failed");
                    ++count;
                }
            }
            parser.finish();
            destination.flush();
            if (!destination) throw std::runtime_error("Capture flush failed");
            std::cerr << "Capture: frames=" << count << " invalid=" << parser.invalid_frames() << " clipped=" << clipped << '\n';
            if (!count) throw std::runtime_error("No valid serial frames before timeout");
            return 0;
        }
        if (input.empty() || output.empty()) throw std::invalid_argument("Use --replay INPUT --output OUTPUT, or --help");
        if (std::filesystem::weakly_canonical(input) == std::filesystem::weakly_canonical(output) ||
            (std::filesystem::exists(output) && std::filesystem::equivalent(input, output)))
            throw std::invalid_argument("Input and output must differ");
        std::ifstream source(input, std::ios::binary);
        if (!source) throw std::runtime_error("Cannot open input");
        std::ofstream destination(output, std::ios::binary | std::ios::trunc);
        if (!destination) throw std::runtime_error("Cannot open output");
        posmv::Tss1Parser parser;
        std::array<char, 4096> bytes{};
        std::uint64_t count{}, clipped{};
        while (source.read(bytes.data(), bytes.size()) || source.gcount() > 0) {
            for (const auto& frame : parser.feed(std::string_view(bytes.data(), source.gcount()))) {
                const auto roll = posmv::compose_roll(frame.roll_deg,
                    posmv::synthetic_roll(config, static_cast<double>(count) / rate));
                if (roll.clipped) ++clipped;
                destination << (config.enabled ? posmv::encode_roll(frame, roll.roll_deg) : frame.raw + "\r\n");
                ++count;
            }
        }
        if (source.bad()) throw std::runtime_error("Input read failed");
        parser.finish();
        destination.flush();
        if (!destination) throw std::runtime_error("Output write failed");
        std::cerr << "Replay: output=" << count << " invalid=" << parser.invalid_frames()
                  << " clipped=" << clipped << '\n';
        if (count == 0) throw std::runtime_error("No valid TSS1 frames");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "POSMV-Sweep: " << e.what() << '\n';
        return 1;
    }
}