#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "replicate.hpp"
#include "synth.hpp"

using namespace dedup;

TEST(replicate_copies_an_object_to_an_empty_remote) {
    ChunkStore src, dst;
    auto data = synth::random_bytes(2 << 20, 41);
    Recipe r = src.put(data);
    ReplicateResult res = replicate(src, r, dst);
    CHECK(dst.get(r) == data);
    CHECK(dst.audit({r}).empty());
    CHECK(res.chunks_already_there == 0);
    CHECK(res.chunks_sent == dst.chunk_count());
    // Protocol overhead (recipe and per-chunk headers) stays under 2% of the data.
    CHECK(res.bytes_to_remote > data.size());
    CHECK(res.bytes_to_remote * 100 < data.size() * 102);
}

TEST(replicate_sends_only_the_chunks_that_changed) {
    ChunkStore src, dst;
    synth::Rng rng(5);
    auto v1 = synth::random_bytes(4 << 20, 51);
    auto v2 = synth::mutate(v1, 3, rng);
    Recipe r1 = src.put(v1);
    Recipe r2 = src.put(v2);
    replicate(src, r1, dst);
    ReplicateResult res = replicate(src, r2, dst);
    CHECK(res.chunks_sent > 0);
    CHECK(res.chunks_sent < 10);
    CHECK(res.bytes_to_remote * 20 < v2.size());  // under 5% of the object
    CHECK(dst.get(r1) == v1);
    CHECK(dst.get(r2) == v2);
    CHECK(dst.audit({r1, r2}).empty());
    dst.remove(r1);
    dst.remove(r2);
    CHECK(dst.stored_bytes() == 0);
}

TEST(replicate_an_object_the_remote_already_has_sends_no_chunks) {
    ChunkStore src, dst;
    auto data = synth::random_bytes(1 << 20, 61);
    Recipe r = src.put(data);
    replicate(src, r, dst);
    ReplicateResult res = replicate(src, r, dst);
    CHECK(res.chunks_sent == 0);
    CHECK(res.bytes_to_remote < 8 + 32 * r.chunks.size() + 1);
    CHECK(dst.audit({r, r}).empty());
}

TEST(replicate_handles_a_digest_that_repeats_inside_one_object) {
    ChunkStore src, dst;
    auto block = synth::random_bytes(100 * 1024, 71);
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 4; ++i) data.insert(data.end(), block.begin(), block.end());
    Recipe r = src.put(data);
    replicate(src, r, dst);
    CHECK(dst.get(r) == data);
    CHECK(dst.audit({r}).empty());
    dst.remove(r);
    CHECK(dst.stored_bytes() == 0);
}

// The remote deletes the older version between answering and receiving the newer one.
// Chunks both versions share were present when the remote answered. Because the answer
// took a reference, they survive the delete.
TEST(replicate_survives_a_delete_on_the_remote_while_in_flight) {
    ChunkStore src, dst;
    synth::Rng rng(8);
    auto v1 = synth::random_bytes(2 << 20, 81);
    auto v2 = synth::mutate(v1, 3, rng);
    Recipe r1 = src.put(v1);
    Recipe r2 = src.put(v2);
    Recipe on_remote = dst.put(v1);

    ReplicateHooks hooks;
    hooks.after_probe = [&] { dst.remove(on_remote); };
    ReplicateResult res = replicate(src, r2, dst, hooks);
    CHECK(res.chunks_already_there > 0);
    CHECK(dst.get(r2) == v2);
    CHECK(dst.audit({r2}).empty());
}

// The same scenario with a protocol that only asks "do you have it?" and takes its
// references at the end. This is the control: it must lose chunks, which is why the
// answer in replicate() takes the references.
static std::size_t unpinned_dangling(const ChunkStore& src, const Recipe& r, ChunkStore& dst,
                                     const std::function<void()>& after_probe) {
    std::map<Digest, std::uint32_t> wanted;
    for (const Digest& d : r.chunks) ++wanted[d];
    std::vector<std::pair<Digest, std::uint32_t>> present, missing;
    for (const auto& [d, n] : wanted) (dst.contains(d) ? present : missing).push_back({d, n});
    after_probe();
    for (const auto& [d, n] : missing) {
        std::vector<std::uint8_t> data;
        src.read_chunk(d, data);
        dst.insert(d, std::move(data), n);
    }
    std::size_t dangling = 0;
    for (const auto& [d, n] : present)
        if (!dst.acquire(d, n)) ++dangling;
    return dangling;
}

TEST(replicate_control_unpinned_protocol_loses_chunks) {
    ChunkStore src, dst;
    synth::Rng rng(8);
    auto v1 = synth::random_bytes(2 << 20, 81);
    auto v2 = synth::mutate(v1, 3, rng);
    src.put(v1);
    Recipe r2 = src.put(v2);
    Recipe on_remote = dst.put(v1);
    std::size_t dangling = unpinned_dangling(src, r2, dst, [&] { dst.remove(on_remote); });
    CHECK(dangling > 0);
}

TEST(replicate_failure_leaves_the_remote_as_it_was) {
    ChunkStore src, dst;
    synth::Rng rng(9);
    auto v1 = synth::random_bytes(2 << 20, 91);
    auto v2 = synth::mutate(v1, 3, rng);
    Recipe r1 = src.put(v1);
    Recipe r2 = src.put(v2);
    Recipe on_remote = dst.put(v1);
    std::uint64_t before = dst.stored_bytes();

    // The source drops the new version after the remote has answered, so a chunk it was
    // asked for is gone.
    ReplicateHooks hooks;
    hooks.after_probe = [&] { src.remove(r2); };
    bool threw = false;
    try {
        replicate(src, r2, dst, hooks);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(dst.stored_bytes() == before);
    CHECK(dst.audit({on_remote}).empty());
    CHECK(src.audit({r1}).empty());
}
