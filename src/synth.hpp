#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// Deterministic synthetic data for the tests and the benchmark: xorshift64*, so a seed
// always gives the same bytes. Nothing here is real user data.
namespace synth {

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

// A new version of `base`: `edits` random changes, each an insertion of 1-256 bytes
// (which shifts everything after it), a deletion of 1-256 bytes, or a 4 KiB overwrite.
inline std::vector<std::uint8_t> mutate(std::vector<std::uint8_t> base, std::size_t edits, Rng& rng) {
    for (std::size_t i = 0; i < edits; ++i) {
        std::size_t pos = rng.below(base.size());
        switch (rng.below(3)) {
            case 0: {
                std::size_t len = 1 + rng.below(256);
                std::vector<std::uint8_t> ins(len);
                for (auto& b : ins) b = static_cast<std::uint8_t>(rng.next() >> 56);
                base.insert(base.begin() + static_cast<std::ptrdiff_t>(pos), ins.begin(), ins.end());
                break;
            }
            case 1: {
                std::size_t len = std::min<std::size_t>(1 + rng.below(256), base.size() - pos);
                base.erase(base.begin() + static_cast<std::ptrdiff_t>(pos),
                           base.begin() + static_cast<std::ptrdiff_t>(pos + len));
                break;
            }
            default: {
                std::size_t len = std::min<std::size_t>(4096, base.size() - pos);
                for (std::size_t k = 0; k < len; ++k) base[pos + k] = static_cast<std::uint8_t>(rng.next() >> 56);
                break;
            }
        }
    }
    return base;
}

}  // namespace synth
