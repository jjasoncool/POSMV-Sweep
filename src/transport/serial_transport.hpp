#pragma once
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
namespace posmv {
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void write(std::string_view bytes) = 0;
    virtual std::size_t read(std::span<char> bytes) = 0;
};
class SerialTransport final : public ITransport {
public:
    SerialTransport(const std::string& port, unsigned baud);
    ~SerialTransport() override;
    SerialTransport(const SerialTransport&) = delete;
    SerialTransport& operator=(const SerialTransport&) = delete;
    void write(std::string_view bytes) override;
    std::size_t read(std::span<char> bytes) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
inline void validate_serial_throughput(unsigned baud, unsigned rate) {
    if (baud == 0 || static_cast<unsigned long long>(baud) < 27ULL * 10 * rate)
        throw std::invalid_argument("Baud insufficient for 27-byte TSS1 frames at 8N1 selected rate");
}
inline constexpr unsigned standard_bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};
// The lowest standard baud that carries 27-byte 8N1 TSS1 frames at `rate` Hz.
inline unsigned minimum_baud(unsigned rate) {
    for (const unsigned baud : standard_bauds)
        if (baud >= 27ULL * 10 * rate) return baud;
    throw std::invalid_argument("No standard baud carries this TSS1 rate");
}
}