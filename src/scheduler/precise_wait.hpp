#pragma once
#include <chrono>
namespace posmv {
// Sleep until `deadline` with sub-millisecond precision where the platform allows it;
// the default Windows timer granularity (about 15.6 ms) would make 25-100 Hz output jitter.
void wait_until(std::chrono::steady_clock::time_point deadline);
}
