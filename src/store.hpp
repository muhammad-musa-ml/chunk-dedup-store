#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "chunker.hpp"
#include "sha256.hpp"

namespace dedup {

enum class Chunking { ContentDefined, Fixed };

struct StoreConfig {
    Chunking chunking = Chunking::ContentDefined;
    ChunkerConfig cdc;
    std::size_t fixed_size = 8192;
};

// What a stored object is: the chunk digests in order. A digest listed twice holds two references.
struct Recipe {
    std::vector<Digest> chunks;
    std::uint64_t size = 0;
};

// A chunk is stored once and counted by how many recipe entries refer to it. It is freed the
// moment its count reaches zero, so reclamation needs no separate pass. Every operation on one
// chunk happens under that chunk's shard lock.
class ChunkStore {
public:
    explicit ChunkStore(StoreConfig cfg = {});
    ChunkStore(const ChunkStore&) = delete;
    ChunkStore& operator=(const ChunkStore&) = delete;

    Recipe put(const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> get(const Recipe& r) const;  // throws if a chunk is missing
    // Drops one reference per entry and returns the bytes freed. Removing a recipe twice is a
    // caller error. It throws once a chunk has no references left, but while other objects hold
    // the same chunks a second remove cannot be told from a real one and takes their references.
    // After a throw the recipe is partly removed.
    std::uint64_t remove(const Recipe& r);

    // Pieces the replication step uses.
    bool contains(const Digest& d) const;
    bool read_chunk(const Digest& d, std::vector<std::uint8_t>& out) const;
    bool acquire(const Digest& d, std::uint32_t refs);  // add refs to a chunk that is present
    void insert(const Digest& d, std::vector<std::uint8_t> data, std::uint32_t refs);
    std::uint64_t release(const Digest& d, std::uint32_t refs);

    std::uint64_t stored_bytes() const;
    std::size_t chunk_count() const;

    // Compare the stored reference counts with what the live recipes say they should be.
    // Reports leaks (count too high or no recipe refers to the chunk) and dangling references
    // (a recipe refers to a chunk that is gone). Call it while no other thread is writing.
    std::vector<std::string> audit(const std::vector<Recipe>& live) const;

private:
    struct Entry {
        std::vector<std::uint8_t> data;
        std::uint32_t refs = 0;
    };
    struct Shard {
        mutable std::mutex m;
        std::unordered_map<Digest, Entry, DigestHash> map;
        std::uint64_t bytes = 0;
    };
    static constexpr std::size_t kShards = 16;

    // Counts are 32-bit; adding past the end throws instead of wrapping to zero.
    static void add_refs(Entry& e, std::uint32_t n);
    Shard& shard_for(const Digest& d) { return shards_[d[0] % kShards]; }
    const Shard& shard_for(const Digest& d) const { return shards_[d[0] % kShards]; }
    std::vector<std::size_t> split(const std::vector<std::uint8_t>& data) const;

    StoreConfig cfg_;
    std::array<Shard, kShards> shards_;
};

}  // namespace dedup
