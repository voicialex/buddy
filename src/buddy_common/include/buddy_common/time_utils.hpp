#pragma once

#include <chrono>

namespace buddy {

using TimePoint = std::chrono::steady_clock::time_point;

inline TimePoint now() {
    return std::chrono::steady_clock::now();
}

inline double elapsed_ms(TimePoint from, TimePoint to) {
    if (from == TimePoint{} || to == TimePoint{}) return -1.0;
    return std::chrono::duration<double, std::milli>(to - from).count();
}

inline double elapsed_sec(TimePoint from, TimePoint to) {
    if (from == TimePoint{} || to == TimePoint{}) return -1.0;
    return std::chrono::duration<double>(to - from).count();
}

}  // namespace buddy
