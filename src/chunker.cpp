#include "chunker.hpp"

#include <array>
#include <stdexcept>

namespace dedup {
namespace {

std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// Fixed seed: the same bytes always cut at the same places, on every run and machine.
const std::array<std::uint64_t, 256>& gear_table() {
    static const std::array<std::uint64_t, 256> table = [] {
        std::array<std::uint64_t, 256> t{};
        std::uint64_t state = 0x6368756e6b73ULL;
        for (auto& v : t) v = splitmix64(state);
        return t;
    }();
    return table;
}

// The hash shifts left each byte, so a byte's influence climbs toward the top bits and
// falls off after 64 bytes. Masks therefore use the top bits: they cover a 64-byte window.
std::uint64_t top_mask(unsigned bits) { return ~std::uint64_t{0} << (64 - bits); }

unsigned log2_exact(std::size_t v) {
    unsigned b = 0;
    while ((std::size_t{1} << b) < v) ++b;
    return b;
}

struct Masks {
    std::uint64_t strict;
    std::uint64_t loose;
};

// Length of the next chunk of p[0, left).
std::size_t next_cut(const std::uint8_t* p, std::size_t left, const ChunkerConfig& cfg,
                     const Masks& m) {
    if (left <= cfg.min_size) return left;
    const auto& gear = gear_table();
    const std::size_t limit = left < cfg.max_size ? left : cfg.max_size;
    const std::size_t normal = left < cfg.avg_size ? left : cfg.avg_size;
    std::uint64_t fp = 0;
    std::size_t i = cfg.min_size;
    // Before the average size the mask is stricter, after it looser, which keeps sizes near the average.
    for (; i < normal; ++i) {
        fp = (fp << 1) + gear[p[i]];
        if ((fp & m.strict) == 0) return i + 1;
    }
    for (; i < limit; ++i) {
        fp = (fp << 1) + gear[p[i]];
        if ((fp & m.loose) == 0) return i + 1;
    }
    return limit;
}

}  // namespace

std::vector<std::size_t> cdc_chunks(const std::uint8_t* data, std::size_t n,
                                    const ChunkerConfig& cfg) {
    if (cfg.avg_size == 0 || (cfg.avg_size & (cfg.avg_size - 1)) != 0 ||
        cfg.min_size >= cfg.avg_size || cfg.avg_size >= cfg.max_size) {
        throw std::invalid_argument("chunker needs min < avg < max and avg a power of two");
    }
    const unsigned bits = log2_exact(cfg.avg_size);
    const Masks masks{top_mask(bits + 2), top_mask(bits - 2)};
    std::vector<std::size_t> out;
    for (std::size_t pos = 0; pos < n;) {
        std::size_t len = next_cut(data + pos, n - pos, cfg, masks);
        out.push_back(len);
        pos += len;
    }
    return out;
}

std::vector<std::size_t> fixed_chunks(std::size_t n, std::size_t size) {
    if (size == 0) throw std::invalid_argument("fixed chunk size must be positive");
    std::vector<std::size_t> out;
    for (std::size_t pos = 0; pos < n; pos += size) out.push_back(n - pos < size ? n - pos : size);
    return out;
}

}  // namespace dedup
