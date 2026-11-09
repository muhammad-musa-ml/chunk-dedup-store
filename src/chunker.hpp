#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dedup {

// Content-defined chunking with a Gear rolling hash and normalized cut masks.
// Whether a position is a cut is decided by a hash of at most the 64 bytes before it, plus
// its distance from the previous cut (the minimum size, the mask switch at the average size
// and the forced cut at the maximum). After an insertion or deletion the cuts fall at the
// same places again within a few chunks, so most of the stream chunks as before.
struct ChunkerConfig {
    std::size_t min_size = 2048;
    std::size_t avg_size = 8192;  // must be a power of two
    std::size_t max_size = 65536;
};

// Lengths of the chunks of data[0, n). The lengths add up to n.
std::vector<std::size_t> cdc_chunks(const std::uint8_t* data, std::size_t n,
                                    const ChunkerConfig& cfg = {});

// Fixed-size chunking, kept as the baseline that content-defined chunking is compared with.
std::vector<std::size_t> fixed_chunks(std::size_t n, std::size_t size);

}  // namespace dedup
