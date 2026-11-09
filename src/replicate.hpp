#pragma once
#include <cstdint>
#include <functional>

#include "store.hpp"

namespace dedup {

struct ReplicateResult {
    std::uint64_t bytes_to_remote = 0;    // recipe, then the chunks the remote did not have
    std::uint64_t bytes_from_remote = 0;  // the remote's answer: which digests it lacks
    std::uint64_t chunks_sent = 0;
    std::uint64_t chunks_already_there = 0;
};

struct ReplicateHooks {
    // Runs after the remote has answered and before any chunk is sent. Tests use it to run a
    // delete on the remote at the worst moment.
    std::function<void()> after_probe;
};

// Copy the object `r` from `src` to `dst`, sending only the chunks `dst` does not hold.
//
// The remote takes its references while it answers: a digest it reports as present is already
// counted for this recipe, so a concurrent remove on the remote cannot free it before the
// recipe is complete. If anything fails, the references taken so far are released and `dst` is
// left as it was. Wire sizes are counted as: 8 bytes + 32 per digest for the recipe, 1 bit per
// distinct digest for the answer, 36 bytes + the data for each chunk sent. They describe this
// protocol, not any real network.
ReplicateResult replicate(const ChunkStore& src, const Recipe& r, ChunkStore& dst,
                          const ReplicateHooks& hooks = {});

}  // namespace dedup
