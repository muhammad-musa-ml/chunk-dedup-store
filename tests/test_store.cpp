#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "store.hpp"
#include "synth.hpp"

using namespace dedup;

static bool throws_logic(ChunkStore& s, const Recipe& r) {
    try {
        s.remove(r);
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

TEST(store_round_trips_small_and_large_objects) {
    ChunkStore s;
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{1000}, std::size_t{2048},
                          std::size_t{300000}}) {
        auto data = synth::random_bytes(n, 100 + n);
        Recipe r = s.put(data);
        CHECK(r.size == n);
        CHECK(s.get(r) == data);
        CHECK(s.audit({r}).empty());
        s.remove(r);
    }
    CHECK(s.chunk_count() == 0);
    CHECK(s.stored_bytes() == 0);
}

TEST(store_keeps_one_copy_of_identical_objects) {
    ChunkStore s;
    auto data = synth::random_bytes(500000, 3);
    Recipe a = s.put(data);
    std::uint64_t after_first = s.stored_bytes();
    Recipe b = s.put(data);
    CHECK(s.stored_bytes() == after_first);
    CHECK(a.chunks == b.chunks);
    CHECK(s.audit({a, b}).empty());

    CHECK(s.remove(a) == 0);  // b still holds every chunk
    CHECK(s.get(b) == data);
    CHECK(s.remove(b) == after_first);
    CHECK(s.stored_bytes() == 0);
}

TEST(store_counts_a_chunk_twice_when_one_object_repeats_it) {
    ChunkStore s;
    // The same 100 KiB block four times over: the chunks repeat inside one recipe.
    auto block = synth::random_bytes(100 * 1024, 5);
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 4; ++i) data.insert(data.end(), block.begin(), block.end());
    Recipe r = s.put(data);
    std::set<Digest> unique(r.chunks.begin(), r.chunks.end());
    CHECK(unique.size() < r.chunks.size());  // the recipe really lists a digest more than once
    CHECK(s.stored_bytes() < data.size() / 2);
    CHECK(s.get(r) == data);
    CHECK(s.audit({r}).empty());
    s.remove(r);
    CHECK(s.stored_bytes() == 0);
}

TEST(store_refuses_a_second_remove_and_a_read_after_remove) {
    ChunkStore s;
    Recipe r = s.put(synth::random_bytes(50000, 9));
    s.remove(r);
    CHECK(throws_logic(s, r));
    bool threw = false;
    try {
        s.get(r);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(store_rejects_a_chunk_that_does_not_match_its_digest) {
    ChunkStore s;
    std::vector<std::uint8_t> bytes{1, 2, 3};
    Digest wrong = sha256(bytes.data(), bytes.size());
    wrong[0] ^= 1;
    bool threw = false;
    try {
        s.insert(wrong, bytes, 1);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(s.chunk_count() == 0);
}

// A count that would wrap to zero must throw and leave the chunk as it was.
TEST(store_refuses_a_reference_count_that_would_overflow) {
    ChunkStore s;
    std::vector<std::uint8_t> blob = synth::random_bytes(100, 17);  // one chunk: shorter than the minimum
    Digest d = sha256(blob.data(), blob.size());
    s.insert(d, blob, 0xFFFFFFFFu);
    bool acquire_threw = false, put_threw = false;
    try {
        s.acquire(d, 1);
    } catch (const std::overflow_error&) {
        acquire_threw = true;
    }
    try {
        s.put(blob);
    } catch (const std::overflow_error&) {
        put_threw = true;
    }
    CHECK(acquire_threw);
    CHECK(put_threw);
    CHECK(s.chunk_count() == 1);
    CHECK(s.release(d, 0xFFFFFFFFu) == blob.size());  // still holds exactly the original count
    CHECK(s.chunk_count() == 0);
    CHECK(s.stored_bytes() == 0);
}

TEST(store_audit_reports_a_leak_and_a_dangling_reference) {
    ChunkStore s;
    Recipe a = s.put(synth::random_bytes(50000, 13));
    CHECK(s.audit({a}).empty());
    CHECK(!s.audit({}).empty());  // chunks held, no recipe refers to them: a leak
    s.remove(a);
    CHECK(!s.audit({a}).empty());  // recipe refers to chunks that are gone: dangling
}
