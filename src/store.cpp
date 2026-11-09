#include "store.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace dedup {

ChunkStore::ChunkStore(StoreConfig cfg) : cfg_(cfg) {}

void ChunkStore::add_refs(Entry& e, std::uint32_t n) {
    if (n > std::numeric_limits<std::uint32_t>::max() - e.refs) {
        throw std::overflow_error("reference count overflow");
    }
    e.refs += n;
}

std::vector<std::size_t> ChunkStore::split(const std::vector<std::uint8_t>& data) const {
    if (cfg_.chunking == Chunking::Fixed) return fixed_chunks(data.size(), cfg_.fixed_size);
    return cdc_chunks(data.data(), data.size(), cfg_.cdc);
}

Recipe ChunkStore::put(const std::vector<std::uint8_t>& data) {
    Recipe r;
    r.size = data.size();
    std::size_t off = 0;
    try {
        const std::vector<std::size_t> lens = split(data);
        // Reserved up front so recording a digest below cannot fail after its reference is taken.
        r.chunks.reserve(lens.size());
        for (std::size_t len : lens) {
            const std::uint8_t* p = data.data() + off;
            Digest d = sha256(p, len);
            Shard& s = shard_for(d);
            std::lock_guard<std::mutex> lock(s.m);
            auto it = s.map.find(d);
            if (it != s.map.end()) {
                // Same digest, different bytes would corrupt the store silently; refuse instead.
                if (it->second.data.size() != len || !std::equal(p, p + len, it->second.data.begin())) {
                    throw std::runtime_error("digest collision");
                }
                add_refs(it->second, 1);
            } else {
                s.map.emplace(d, Entry{std::vector<std::uint8_t>(p, p + len), 1});
                s.bytes += len;
            }
            r.chunks.push_back(d);
            off += len;
        }
    } catch (...) {
        for (const Digest& d : r.chunks) release(d, 1);
        throw;
    }
    return r;
}

std::vector<std::uint8_t> ChunkStore::get(const Recipe& r) const {
    std::vector<std::uint8_t> out;
    out.reserve(r.size);
    for (const Digest& d : r.chunks) {
        const Shard& s = shard_for(d);
        std::lock_guard<std::mutex> lock(s.m);
        auto it = s.map.find(d);
        if (it == s.map.end()) throw std::runtime_error("missing chunk " + to_hex(d).substr(0, 12));
        out.insert(out.end(), it->second.data.begin(), it->second.data.end());
    }
    return out;
}

std::uint64_t ChunkStore::remove(const Recipe& r) {
    std::uint64_t freed = 0;
    for (const Digest& d : r.chunks) freed += release(d, 1);
    return freed;
}

bool ChunkStore::contains(const Digest& d) const {
    const Shard& s = shard_for(d);
    std::lock_guard<std::mutex> lock(s.m);
    return s.map.count(d) != 0;
}

bool ChunkStore::read_chunk(const Digest& d, std::vector<std::uint8_t>& out) const {
    const Shard& s = shard_for(d);
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.map.find(d);
    if (it == s.map.end()) return false;
    out = it->second.data;
    return true;
}

bool ChunkStore::acquire(const Digest& d, std::uint32_t refs) {
    Shard& s = shard_for(d);
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.map.find(d);
    if (it == s.map.end()) return false;
    add_refs(it->second, refs);
    return true;
}

void ChunkStore::insert(const Digest& d, std::vector<std::uint8_t> data, std::uint32_t refs) {
    if (sha256(data.data(), data.size()) != d) throw std::runtime_error("chunk does not match its digest");
    Shard& s = shard_for(d);
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.map.find(d);
    if (it != s.map.end()) {
        if (it->second.data != data) throw std::runtime_error("digest collision");
        add_refs(it->second, refs);
        return;
    }
    const std::uint64_t size = data.size();
    s.map.emplace(d, Entry{std::move(data), refs});
    s.bytes += size;  // after the emplace, so a failed allocation leaves the count right
}

std::uint64_t ChunkStore::release(const Digest& d, std::uint32_t refs) {
    Shard& s = shard_for(d);
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.map.find(d);
    if (it == s.map.end() || it->second.refs < refs) {
        throw std::logic_error("release of a chunk that is not held " + to_hex(d).substr(0, 12));
    }
    it->second.refs -= refs;
    if (it->second.refs != 0) return 0;
    std::uint64_t freed = it->second.data.size();
    s.bytes -= freed;
    s.map.erase(it);
    return freed;
}

std::uint64_t ChunkStore::stored_bytes() const {
    std::uint64_t total = 0;
    for (const Shard& s : shards_) {
        std::lock_guard<std::mutex> lock(s.m);
        total += s.bytes;
    }
    return total;
}

std::size_t ChunkStore::chunk_count() const {
    std::size_t total = 0;
    for (const Shard& s : shards_) {
        std::lock_guard<std::mutex> lock(s.m);
        total += s.map.size();
    }
    return total;
}

std::vector<std::string> ChunkStore::audit(const std::vector<Recipe>& live) const {
    std::map<Digest, std::uint32_t> expected;
    for (const Recipe& r : live)
        for (const Digest& d : r.chunks) ++expected[d];

    std::vector<std::string> problems;
    std::map<Digest, bool> seen;
    for (const Shard& s : shards_) {
        std::lock_guard<std::mutex> lock(s.m);
        for (const auto& [d, e] : s.map) {
            seen[d] = true;
            auto it = expected.find(d);
            std::uint32_t want = it == expected.end() ? 0 : it->second;
            if (e.refs != want) {
                problems.push_back("count " + to_hex(d).substr(0, 12) + " holds " + std::to_string(e.refs) +
                                   " expected " + std::to_string(want));
            }
        }
    }
    for (const auto& [d, n] : expected) {
        if (!seen.count(d)) problems.push_back("dangling " + to_hex(d).substr(0, 12));
    }
    return problems;
}

}  // namespace dedup
