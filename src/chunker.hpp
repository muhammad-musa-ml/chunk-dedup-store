#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dedup {

// Content-defined chunking with a Gear rolling hash and normalized cut masks.
// A cut depends on the 64 bytes before it, so inserting or deleting bytes only
// moves the boundaries near the edit; the rest of the stream cuts at the same places.
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
