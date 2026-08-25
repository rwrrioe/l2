#pragma once
#include <cstdint>
#include <ctime>

inline uint64_t now_ns() noexcept {
    timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000'000ull + uint64_t(ts.tv_nsec);
}

inline int64_t mono_ns() noexcept {
    timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1'000'000'000ll + int64_t(ts.tv_nsec);
}
