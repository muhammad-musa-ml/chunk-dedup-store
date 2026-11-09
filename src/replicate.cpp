#include "replicate.hpp"

#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dedup {

ReplicateResult replicate(const ChunkStore& src, const Recipe& r, ChunkStore& dst,
                          const ReplicateHooks& hooks) {
    ReplicateResult res;
    res.bytes_to_remote += 8 + 32 * r.chunks.size();

    // A digest can appear several times in one recipe; each appearance is one reference.
    std::map<Digest, std::uint32_t> wanted;
    for (const Digest& d : r.chunks) ++wanted[d];

    using Held = std::vector<std::pair<Digest, std::uint32_t>>;
    Held held;     // references the remote now holds for this recipe
    Held missing;  // digests the remote answered it does not have
    try {
        for (const auto& [d, n] : wanted) {
            if (dst.acquire(d, n)) {
                held.push_back({d, n});
                ++res.chunks_already_there;
            } else {
                missing.push_back({d, n});
            }
        }
        res.bytes_from_remote += (wanted.size() + 7) / 8;

        if (hooks.after_probe) hooks.after_probe();

        for (const auto& [d, n] : missing) {
            std::vector<std::uint8_t> data;
            if (!src.read_chunk(d, data)) throw std::runtime_error("source no longer has a chunk");
            res.bytes_to_remote += 36 + data.size();
            dst.insert(d, std::move(data), n);
            held.push_back({d, n});
            ++res.chunks_sent;
        }
    } catch (...) {
        for (const auto& [d, n] : held) dst.release(d, n);
        throw;
    }
    return res;
}

}  // namespace dedup
