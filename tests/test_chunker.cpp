#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "chunker.hpp"
#include "sha256.hpp"
#include "synth.hpp"

using namespace dedup;

static std::size_t sum(const std::vector<std::size_t>& v) {
    std::size_t s = 0;
    for (auto x : v) s += x;
    return s;
}

// Digests of every chunk of `data`, cut by `lens`.
static std::set<Digest> digests_of(const std::vector<std::uint8_t>& data,
                                   const std::vector<std::size_t>& lens) {
    std::set<Digest> out;
    std::size_t off = 0;
    for (auto len : lens) {
        out.insert(sha256(data.data() + off, len));
        off += len;
    }
    return out;
}

TEST(chunker_lengths_add_up_and_stay_in_bounds) {
    ChunkerConfig cfg;
    // Only the last chunk may be shorter than the minimum. 65536 and 65537 sit on the maximum.
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{2048}, std::size_t{2049},
                          std::size_t{65536}, std::size_t{65537}, std::size_t{70000},
                          std::size_t{1 << 20}}) {
        auto data = synth::random_bytes(n, 7 + n);
        auto lens = cdc_chunks(data.data(), n, cfg);
        CHECK(sum(lens) == n);
        for (std::size_t i = 0; i < lens.size(); ++i) {
            CHECK(lens[i] <= cfg.max_size);
            if (i + 1 < lens.size()) CHECK(lens[i] >= cfg.min_size);
        }
    }
}

TEST(chunker_is_deterministic) {
    auto data = synth::random_bytes(1 << 20, 11);
    CHECK(cdc_chunks(data.data(), data.size()) == cdc_chunks(data.data(), data.size()));
}

// Pinned lengths for seeded data: a gear table that changed between runs or machines, or a
// change to the cut rule, moves these. Recorded from a run of this code.
TEST(chunker_cuts_seeded_data_at_pinned_places) {
    auto data = synth::random_bytes(1 << 20, 11);
    auto lens = cdc_chunks(data.data(), data.size());
    const std::vector<std::size_t> first8 = {4497, 11410, 9410, 8573, 10009, 8473, 2106, 8336};
    CHECK(lens.size() == 117);
    CHECK(lens.size() >= first8.size());
    for (std::size_t i = 0; i < first8.size() && i < lens.size(); ++i) CHECK(lens[i] == first8[i]);
}

TEST(chunker_average_size_is_near_the_target) {
    ChunkerConfig cfg;
    auto data = synth::random_bytes(8 << 20, 21);
    auto lens = cdc_chunks(data.data(), data.size(), cfg);
    double avg = static_cast<double>(data.size()) / static_cast<double>(lens.size());
    CHECK(avg > 0.7 * cfg.avg_size && avg < 1.5 * cfg.avg_size);
}

TEST(chunker_max_size_forces_a_cut_on_constant_data) {
    ChunkerConfig cfg;
    std::vector<std::uint8_t> zeros(1 << 20, 0);
    auto lens = cdc_chunks(zeros.data(), zeros.size(), cfg);
    CHECK(sum(lens) == zeros.size());
    // The hash of constant data never meets the cut masks, so every cut here is the forced one.
    CHECK(lens.size() == zeros.size() / cfg.max_size);
    for (auto len : lens) CHECK(len == cfg.max_size);
}

static bool rejected(const ChunkerConfig& cfg) {
    try {
        std::uint8_t b = 0;
        cdc_chunks(&b, 1, cfg);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

TEST(chunker_rejects_bad_config) {
    ChunkerConfig c;
    c.avg_size = 3000;  // not a power of two
    CHECK(rejected(c));
    c = ChunkerConfig{};
    c.min_size = c.avg_size;  // min must be below avg
    CHECK(rejected(c));
    c = ChunkerConfig{};
    c.max_size = c.avg_size;  // avg must be below max
    CHECK(rejected(c));
    c = ChunkerConfig{4, 8, 16};  // too small for the cut masks
    CHECK(rejected(c));
    c = ChunkerConfig{1, 4, 16};
    CHECK(rejected(c));
    CHECK(!rejected(ChunkerConfig{4, 16, 64}));
}

// The reason to cut on content: one inserted byte near the front shifts every later byte.
// Fixed-size chunks all change; content-defined chunks resynchronize after the edit.
TEST(chunker_resynchronizes_after_an_insertion) {
    auto base = synth::random_bytes(4 << 20, 33);
    auto edited = base;
    edited.insert(edited.begin() + 1000, 0x5a);

    auto cdc_base = digests_of(base, cdc_chunks(base.data(), base.size()));
    auto cdc_edit = digests_of(edited, cdc_chunks(edited.data(), edited.size()));
    std::size_t shared = 0;
    for (const auto& d : cdc_edit) shared += cdc_base.count(d);
    CHECK(shared * 100 >= cdc_edit.size() * 99);  // all but the chunk or two around the edit

    auto fix_base = digests_of(base, fixed_chunks(base.size(), 8192));
    auto fix_edit = digests_of(edited, fixed_chunks(edited.size(), 8192));
    std::size_t fixed_shared = 0;
    for (const auto& d : fix_edit) fixed_shared += fix_base.count(d);
    CHECK(fixed_shared * 100 <= fix_edit.size());  // at most 1%
}
