// Writes results/summary.md. Every number in the README is copied from that file.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "chunker.hpp"
#include "replicate.hpp"
#include "store.hpp"
#include "synth.hpp"

using namespace dedup;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kBase = 16u << 20;  // 16 MiB first version
constexpr int kVersions = 10;
constexpr std::size_t kEdits = 24;  // per version
constexpr int kSeeds = 5;

struct Range {
    double lo = 1e300, hi = -1e300;
    void add(double v) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
};

std::string range_str(const Range& r, const char* fmt) {
    char a[64], b[64], out[160];
    std::snprintf(a, sizeof a, fmt, r.lo);
    std::snprintf(b, sizeof b, fmt, r.hi);
    std::snprintf(out, sizeof out, "%s - %s", a, b);
    return out;
}

std::vector<std::vector<std::uint8_t>> make_versions(std::uint64_t seed) {
    std::vector<std::vector<std::uint8_t>> v;
    synth::Rng rng(seed * 7919);
    v.push_back(synth::random_bytes(kBase, seed));
    for (int i = 1; i < kVersions; ++i) v.push_back(synth::mutate(v.back(), kEdits, rng));
    return v;
}

void must(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "bench check failed: %s\n", what);
        std::exit(1);
    }
}

struct Timing {
    std::vector<double> mbps;
    std::string str() const {
        std::vector<double> s = mbps;
        std::sort(s.begin(), s.end());
        char out[160];
        std::snprintf(out, sizeof out, "%.0f MB/s (%.0f - %.0f)", s[s.size() / 2], s.front(), s.back());
        return out;
    }
};

}  // namespace

int main() {
    Range cdc_ratio, fixed_ratio, control_ratio;
    Range cdc_wire, fixed_wire, cdc_first_wire;
    Range cdc_chunks_sent, fixed_chunks_sent;
    Range leaked_cdc, leaked_fixed, freed_cdc;
    Timing chunk_only, full_put;

    StoreConfig cdc_cfg;
    StoreConfig fixed_cfg;
    fixed_cfg.chunking = Chunking::Fixed;

    for (int seed = 1; seed <= kSeeds; ++seed) {
        auto versions = make_versions(static_cast<std::uint64_t>(seed));
        std::uint64_t logical = 0;
        for (const auto& v : versions) logical += v.size();

        struct Run {
            StoreConfig cfg;
            Range* ratio;
            Range* wire;
            Range* sent;
            Range* leaked;
        };
        Range unused_freed;
        Run runs[2] = {{cdc_cfg, &cdc_ratio, &cdc_wire, &cdc_chunks_sent, &leaked_cdc},
                       {fixed_cfg, &fixed_ratio, &fixed_wire, &fixed_chunks_sent, &leaked_fixed}};
        for (int k = 0; k < 2; ++k) {
            ChunkStore src(runs[k].cfg), dst(runs[k].cfg);
            std::vector<Recipe> recipes;
            for (const auto& v : versions) recipes.push_back(src.put(v));
            runs[k].ratio->add(static_cast<double>(logical) / static_cast<double>(src.stored_bytes()));
            must(src.get(recipes.back()) == versions.back(), "source round trip");

            // Replicate in order; version 1 goes to an empty remote, the rest to a remote that has the earlier ones.
            std::uint64_t wire_rest = 0, raw_rest = 0, sent_rest = 0;
            for (int i = 0; i < kVersions; ++i) {
                ReplicateResult res = replicate(src, recipes[static_cast<std::size_t>(i)], dst);
                std::uint64_t wire = res.bytes_to_remote + res.bytes_from_remote;
                if (i == 0) {
                    if (k == 0) cdc_first_wire.add(static_cast<double>(wire) / static_cast<double>(versions[0].size()));
                } else {
                    wire_rest += wire;
                    raw_rest += versions[static_cast<std::size_t>(i)].size();
                    sent_rest += res.chunks_sent;
                }
            }
            runs[k].wire->add(static_cast<double>(wire_rest) / static_cast<double>(raw_rest));
            runs[k].sent->add(static_cast<double>(sent_rest) / (kVersions - 1));
            must(dst.get(recipes.back()) == versions.back(), "remote round trip");
            must(dst.audit(recipes).empty(), "remote audit");

            // Reclamation: drop versions 1-5 on both sides; compare with a store that only ever saw 6-10.
            ChunkStore fresh(runs[k].cfg);
            std::vector<Recipe> kept;
            for (int i = 5; i < kVersions; ++i) kept.push_back(fresh.put(versions[static_cast<std::size_t>(i)]));
            std::uint64_t freed = 0;
            for (int i = 0; i < 5; ++i) {
                freed += src.remove(recipes[static_cast<std::size_t>(i)]);
                dst.remove(recipes[static_cast<std::size_t>(i)]);
            }
            std::uint64_t a = src.stored_bytes(), b = fresh.stored_bytes(), c = dst.stored_bytes();
            std::uint64_t leak = (a > b ? a - b : b - a) + (c > b ? c - b : b - c);
            runs[k].leaked->add(static_cast<double>(leak));
            if (k == 0) freed_cdc.add(static_cast<double>(freed));
            std::vector<Recipe> survivors(recipes.begin() + 5, recipes.end());
            must(src.audit(survivors).empty() && dst.audit(survivors).empty(), "reclamation audit");
        }

        // Control: ten unrelated objects of the same size must not look deduplicated.
        {
            ChunkStore s(cdc_cfg);
            std::uint64_t total = 0;
            for (int i = 0; i < kVersions; ++i) {
                auto blob = synth::random_bytes(kBase, 1000 + static_cast<std::uint64_t>(seed) * 100 + static_cast<std::uint64_t>(i));
                total += blob.size();
                s.put(blob);
            }
            control_ratio.add(static_cast<double>(total) / static_cast<double>(s.stored_bytes()));
        }

        if (seed == 1) {
            for (int run = 0; run < 5; ++run) {
                auto t0 = Clock::now();
                auto lens = cdc_chunks(versions[0].data(), versions[0].size(), cdc_cfg.cdc);
                auto t1 = Clock::now();
                must(!lens.empty(), "chunks");
                ChunkStore s(cdc_cfg);
                auto t2 = Clock::now();
                s.put(versions[0]);
                auto t3 = Clock::now();
                double mb = static_cast<double>(versions[0].size()) / (1 << 20);
                chunk_only.mbps.push_back(mb / std::chrono::duration<double>(t1 - t0).count());
                full_put.mbps.push_back(mb / std::chrono::duration<double>(t3 - t2).count());
            }
        }
    }

    std::string md;
    char line[512];
    auto add = [&](const char* s) { md += s; md += "\n"; };
    std::snprintf(line, sizeof line,
                  "Generated by `make bench` (g++ %s). Synthetic data: version 1 is %zu MiB of seeded random "
                  "bytes; each later version is the one before it with %zu random edits (an insertion or "
                  "deletion of 1-256 bytes, or a 4 KiB overwrite). %d versions, seeds 1-%d; ranges are "
                  "min - max over seeds.",
                  __VERSION__, kBase >> 20, kEdits, kVersions, kSeeds);
    add("# Results");
    add("");
    add(line);
    add("Content-defined chunks: 2 KiB min, 8 KiB average, 64 KiB max. Fixed chunks: 8 KiB.");
    add("");
    add("## Deduplication (bytes written / bytes stored, all 10 versions)");
    add("");
    add("| chunking | ratio |");
    add("|---|---|");
    md += "| content-defined | " + range_str(cdc_ratio, "%.2f") + " |\n";
    md += "| fixed 8 KiB | " + range_str(fixed_ratio, "%.2f") + " |\n";
    md += "| control: 10 unrelated objects, content-defined | " + range_str(control_ratio, "%.2f") + " |\n";
    add("");
    add("## Replication (bytes on the wire / object bytes)");
    add("");
    add("| chunking | version 1 to an empty remote | versions 2-10 after the earlier ones | chunks sent per version |");
    add("|---|---|---|---|");
    md += "| content-defined | " + range_str(cdc_first_wire, "%.4f") + " | " + range_str(cdc_wire, "%.4f") + " | " +
          range_str(cdc_chunks_sent, "%.1f") + " |\n";
    md += "| fixed 8 KiB | n/a | " + range_str(fixed_wire, "%.4f") + " | " + range_str(fixed_chunks_sent, "%.1f") + " |\n";
    add("");
    add("## Reclamation (remove versions 1-5 from the source and the remote)");
    add("");
    add("| | bytes |");
    add("|---|---|");
    md += "| stored bytes minus a store that only ever saw versions 6-10, content-defined (source and remote added) | " +
          range_str(leaked_cdc, "%.0f") + " |\n";
    md += "| same, fixed 8 KiB | " + range_str(leaked_fixed, "%.0f") + " |\n";
    md += "| bytes freed on the source, content-defined | " + range_str(freed_cdc, "%.0f") + " |\n";
    add("");
    add("## Speed (seed 1, 16 MiB, 5 runs, median and range)");
    add("");
    add("| step | throughput |");
    add("|---|---|");
    md += "| content-defined chunking only | " + chunk_only.str() + " |\n";
    md += "| chunk, hash with SHA-256 and store | " + full_put.str() + " |\n";

    std::fputs(md.c_str(), stdout);
    std::FILE* f = std::fopen("results/summary.md", "wb");
    must(f != nullptr, "open results/summary.md");
    std::fputs(md.c_str(), f);
    std::fclose(f);
    return 0;
}
