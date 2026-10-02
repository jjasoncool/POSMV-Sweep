#include "scheduler/precise_wait.hpp"
#include <algorithm>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
namespace posmv {
namespace {
// One high-resolution waitable timer per thread (Windows 10 1803+); null where unsupported.
struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ~Timer() { if (handle) CloseHandle(handle); }
};
}
void wait_until(std::chrono::steady_clock::time_point deadline) {
    thread_local Timer timer;
    for (;;) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return;
        LARGE_INTEGER due;
        // Negative due time is relative, in 100 ns units.
        due.QuadPart = -std::max<long long>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
        if (!timer.handle || !SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
            std::this_thread::sleep_until(deadline);
            return;
        }
        WaitForSingleObject(timer.handle, INFINITE);
    }
}
}
