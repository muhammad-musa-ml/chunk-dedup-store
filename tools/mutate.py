"""Make one broken copy of the source per mutant, under <out>/<name>/.

Usage: python3 tools/mutate.py <repo> <out>
Each mutant replaces one exact piece of text that must occur once; if it does not, the mutant
is reported as UNRUN instead of silently doing nothing. tools/run_mutants.sh then builds and
tests every copy. The repo itself is never changed.
"""
import hashlib
import shutil
import sys
from pathlib import Path

REPO = Path(sys.argv[1])
OUT = Path(sys.argv[2])

# name -> (file, old text, new text, make target)
MUTANTS = {
    "m1_release_never_frees": (
        "src/store.cpp",
        "    s.bytes -= freed;\n    s.map.erase(it);\n    return freed;",
        "    return 0;",
        "test",
    ),
    "m2_replicate_does_not_pin": (
        "src/replicate.cpp",
        "if (dst.acquire(d, n)) {",
        "if (dst.contains(d) && dst.acquire(d, 0)) {",
        "test",
    ),
    "m3_chunker_cuts_at_fixed_size": (
        "src/chunker.cpp",
        "    if (left <= cfg.min_size) return left;",
        "    return left < cfg.avg_size ? left : cfg.avg_size;",
        "test",
    ),
    "m4_failed_replication_keeps_refs": (
        "src/replicate.cpp",
        "        for (const auto& [d, n] : held) dst.release(d, n);\n        throw;",
        "        throw;",
        "test",
    ),
    "m5_release_without_lock": (
        "src/store.cpp",
        "std::uint64_t ChunkStore::release(const Digest& d, std::uint32_t refs) {\n    Shard& s = shard_for(d);\n    std::lock_guard<std::mutex> lock(s.m);",
        "std::uint64_t ChunkStore::release(const Digest& d, std::uint32_t refs) {\n    Shard& s = shard_for(d);",
        "tsan",
    ),
    "m6_put_skips_one_reference": (
        "src/store.cpp",
        "                add_refs(it->second, 1);\n            } else {",
        "            } else {",
        "test",
    ),
    "m7_sha256_wrong_rotation": ("src/sha256.cpp", "rotr(e, 25)", "rotr(e, 24)", "test"),
    "m8_get_reads_in_wrong_order": (
        "src/store.cpp",
        "        out.insert(out.end(), it->second.data.begin(), it->second.data.end());\n    }\n    return out;",
        "        out.insert(out.begin(), it->second.data.begin(), it->second.data.end());\n    }\n    return out;",
        "test",
    ),
    "m9_rollback_forgets_inserted_chunks": (
        "src/replicate.cpp",
        "            dst.insert(d, std::move(data), n);\n            held.push_back({d, n});",
        "            dst.insert(d, std::move(data), n);",
        "test",
    ),
    "m10_reference_count_wraps": (
        "src/store.cpp",
        "    if (n > std::numeric_limits<std::uint32_t>::max() - e.refs) {\n        throw std::overflow_error(\"reference count overflow\");\n    }\n",
        "",
        "test",
    ),
    "m11_chunker_ignores_the_minimum_size": (
        "src/chunker.cpp",
        "    std::size_t i = cfg.min_size;\n",
        "    std::size_t i = 0;\n",
        "test",
    ),
}

OUT.mkdir(parents=True, exist_ok=True)
for name, (rel, old, new, target) in MUTANTS.items():
    dest = OUT / name
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(REPO, dest, ignore=shutil.ignore_patterns(".git", "build"))
    f = dest / rel
    text = f.read_bytes().decode("utf-8")
    count = text.count(old)
    if count != 1:
        print("UNRUN", name, "anchor occurs", count, "times")
        continue
    before = hashlib.md5(text.encode("utf-8")).hexdigest()
    f.write_bytes(text.replace(old, new).encode("utf-8"))
    after = hashlib.md5(f.read_bytes()).hexdigest()
    print("APPLIED", name, "target=" + target, "md5", before, "->", after, "differ=" + str(before != after))
