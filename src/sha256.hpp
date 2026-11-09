#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dedup {

using Digest = std::array<std::uint8_t, 32>;

Digest sha256(const std::uint8_t* data, std::size_t n);
std::string to_hex(const Digest& d);

struct DigestHash {
    std::size_t operator()(const Digest& d) const noexcept;
};

}  // namespace dedup
