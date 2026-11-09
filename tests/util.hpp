#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Deterministic test data: xorshift64*, so every run sees the same bytes.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545f4914f6cdd1dULL;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
};

inline std::vector<std::uint8_t> random_bytes(std::size_t n, std::uint64_t seed) {
    Rng r(seed);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(r.next() >> 56);
    return v;
}
