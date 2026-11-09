#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "check.hpp"
#include "store.hpp"
#include "synth.hpp"

using namespace dedup;

// Pool of blobs that share most of their chunks: three bases and edited copies of each.
std::vector<std::vector<std::uint8_t>> make_pool() {
    std::vector<std::vector<std::uint8_t>> pool;
    synth::Rng rng(77);
    for (std::uint64_t b = 0; b < 3; ++b) {
        auto base = synth::random_bytes(200000, 500 + b);
        pool.push_back(base);
        for (int v = 0; v < 3; ++v) pool.push_back(synth::mutate(base, 3, rng));
    }
    return pool;
}

// Digest -> chunk length, computed straight from the chunker so the check does not
// depend on anything inside ChunkStore.
static std::map<Digest, std::size_t> chunk_sizes(const std::vector<std::uint8_t>& data) {
    std::map<Digest, std::size_t> out;
    std::size_t off = 0;
    for (std::size_t len : cdc_chunks(data.data(), data.size())) {
        out[sha256(data.data() + off, len)] = len;
        off += len;
    }
    return out;
}

TEST(model_random_puts_and_removes_match_an_independent_count) {
    auto pool = make_pool();
    std::vector<std::map<Digest, std::size_t>> sizes;
    for (const auto& blob : pool) sizes.push_back(chunk_sizes(blob));

    ChunkStore store;
    std::vector<std::pair<std::size_t, Recipe>> live;
    synth::Rng rng(2024);
    for (int op = 0; op < 300; ++op) {
        if (live.empty() || rng.below(100) < 55) {
            std::size_t idx = rng.below(pool.size());
            live.push_back({idx, store.put(pool[idx])});
        } else {
            std::size_t pick = rng.below(live.size());
            store.remove(live[pick].second);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(pick));
        }
        std::map<Digest, std::size_t> unique;
        std::vector<Recipe> recipes;
        for (const auto& [idx, r] : live) {
            unique.insert(sizes[idx].begin(), sizes[idx].end());
            recipes.push_back(r);
        }
        std::uint64_t want = 0;
        for (const auto& [d, len] : unique) want += len;
        CHECK(store.stored_bytes() == want);
        CHECK(store.chunk_count() == unique.size());
        if (op % 25 == 0) CHECK(store.audit(recipes).empty());
    }
    for (const auto& [idx, r] : live) store.remove(r);
    CHECK(store.stored_bytes() == 0);
    CHECK(store.chunk_count() == 0);
}

// Removing old versions must leave exactly what a store that only ever saw the newer
// versions holds: no chunk the survivors need is lost, no chunk nobody needs is kept.
TEST(model_reclamation_equals_a_store_that_never_saw_the_removed_objects) {
    auto pool = make_pool();
    ChunkStore full;
    std::vector<Recipe> recipes;
    for (const auto& blob : pool) recipes.push_back(full.put(blob));
    ChunkStore fresh;
    std::vector<Recipe> kept;
    for (std::size_t i = 6; i < pool.size(); ++i) kept.push_back(fresh.put(pool[i]));
    for (std::size_t i = 0; i < 6; ++i) full.remove(recipes[i]);
    CHECK(full.stored_bytes() == fresh.stored_bytes());
    CHECK(full.chunk_count() == fresh.chunk_count());
    std::vector<Recipe> survivors(recipes.begin() + 6, recipes.end());
    CHECK(full.audit(survivors).empty());
    for (std::size_t i = 6; i < pool.size(); ++i) CHECK(full.get(recipes[i]) == pool[i]);
}
