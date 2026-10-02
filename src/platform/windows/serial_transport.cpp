#include "transport/serial_transport.hpp"
#include "platform/windows/win32_error.hpp"
#include <stdexcept>
#include <system_error>
#include <algorithm>
namespace posmv {
namespace {
[[noreturn]] void fail(DWORD error, const std::string& operation) {
    throw std::system_error(static_cast<int>(error), win32_category(), operation);
}
// Reads the error before building any string, which could overwrite it.
[[noreturn]] void fail(const char* operation) {
    const DWORD error = GetLastError();
    fail(error, operation);
}
}
struct SerialTransport::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Impl() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
SerialTransport::SerialTransport(const std::string& port, unsigned baud) : impl_(std::make_unique<Impl>()) {
    if (!port.starts_with("COM") || port.size() < 4 || port[3] == '0' ||
        port.find_first_not_of("0123456789", 3) != std::string::npos || baud == 0)
        throw std::invalid_argument("Port must be COM<number>; baud must be positive");
    impl_->handle = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        fail(error, "無法開啟 " + port);
    }
    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(impl_->handle, &settings)) fail("GetCommState");
    settings.BaudRate = baud;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fParity = FALSE;
    settings.fOutxCtsFlow = settings.fOutxDsrFlow = FALSE;
    settings.fDtrControl = DTR_CONTROL_DISABLE;
    settings.fDsrSensitivity = FALSE;
    settings.fTXContinueOnXoff = TRUE;
    settings.fOutX = settings.fInX = FALSE;
    settings.fErrorChar = settings.fNull = FALSE;
    settings.fRtsControl = RTS_CONTROL_DISABLE;
    settings.fAbortOnError = FALSE;
    if (!SetCommState(impl_->handle, &settings)) fail("SetCommState");
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (!SetCommTimeouts(impl_->handle, &timeouts)) fail("SetCommTimeouts");
}
SerialTransport::~SerialTransport() = default;
void SerialTransport::write(std::string_view bytes) {
    while (!bytes.empty()) {
        DWORD written{};
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), MAXDWORD));
        if (!WriteFile(impl_->handle, bytes.data(), requested, &written, nullptr)) fail("Serial write");
        if (written == 0) throw std::runtime_error("Serial write timed out");
        bytes.remove_prefix(written);
    }
}
std::size_t SerialTransport::read(std::span<char> bytes) {
    DWORD received{};
    if (!ReadFile(impl_->handle, bytes.data(), static_cast<DWORD>(std::min<std::size_t>(bytes.size(), MAXDWORD)),
        &received, nullptr)) fail("Serial read");
    return received;
}
}