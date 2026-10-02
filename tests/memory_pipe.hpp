#pragma once
#include "transport/serial_transport.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
namespace posmv::test {
// One direction of a serial link held in memory; read() waits up to 50 ms like SerialTransport.
class MemoryPipe final : public ITransport {
public:
    void write(std::string_view bytes) override {
        { std::lock_guard lock(mutex_); data_.append(bytes); }
        ready_.notify_all();
    }
    std::size_t read(std::span<char> bytes) override {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, std::chrono::milliseconds(50), [&] { return !data_.empty(); });
        const auto n = std::min(bytes.size(), data_.size());
        std::copy_n(data_.begin(), n, bytes.begin());
        data_.erase(0, n);
        return n;
    }
    // Takes everything written so far, after waiting up to `timeout` for at least `count` bytes.
    std::string take(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock, timeout, [&] { return data_.size() >= count; });
        auto taken = std::move(data_);
        data_.clear();
        return taken;
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::string data_;
};
}
