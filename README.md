# chunk-dedup-store

I built a small C++20 chunk store. It stores each distinct piece of data once, frees a piece
the moment nothing refers to it, and copies an object to a second store by sending only the
pieces the second store does not have.

I wrote it to see the three jobs a storage system's data-efficiency code has to do together
(deduplicate, reclaim, replicate over a slow link) in one place small enough to read, with
tests that fail when any of them is wrong and numbers that come from a run.

## What is in it

- `src/chunker.cpp`: content-defined chunking with a Gear rolling hash and two cut masks
  (the scheme FastCDC uses). A position is a cut when a hash of up to the 64 bytes before it
  meets the mask, so after an insertion the cuts fall at the same places again within a few chunks.
  Fixed-size chunking is there as the baseline.
- `src/sha256.cpp`: SHA-256, the chunk fingerprint. Checked against published vectors and
  the padding boundaries.
- `src/store.cpp`: chunks keyed by digest with a reference count. An object is a recipe, the
  list of its digests. The last reference frees the chunk. `put` compares bytes when a digest
  already exists, so a collision fails loudly. `audit` compares stored counts with what the
  live recipes imply and reports leaks and dangling references. Sixteen shards, one lock each.
- `src/replicate.cpp`: the remote answers which digests it lacks, and takes a reference to
  every digest it already has while it answers. Then only the missing chunks are sent. A
  failed transfer releases what it took.

The reference-taking answer matters. If the remote only says "I have it" and counts the
reference at the end, a delete on the remote in between can free a chunk the incoming object
needs, and the object arrives with a hole. `tests/test_replicate.cpp` runs a delete at that
moment against both designs: this one survives, and a protocol that only asks "do you have
it?" loses chunks.

## Run it

Needs g++ 13 and make; `make mutants` also needs python3.

```
make test      # 27 tests
make tsan      # the threaded tests under ThreadSanitizer (Linux; uses setarch -R for WSL2)
make bench     # writes results/summary.md
make mutants   # breaks the code 11 ways in copies and checks the tests notice
```

`make test` ends with `27 tests, <n> checks, 0 failures`; `make tsan` ends with
`2 tests, <n> checks, 0 failures` and prints no ThreadSanitizer warning. The check count
changes from run to run because the reader thread loops until the writers finish.

## What the tests check

- Chunk lengths add up, none is above the maximum and all but the last are at least the
  minimum. The cuts for seeded data are pinned, and constant data is cut at the maximum.
- One inserted byte leaves all but a chunk or two unchanged; with fixed chunks nearly all
  change.
- A random sequence of 300 puts and removes leaves exactly the bytes an independent count
  from the chunker predicts, and removing old objects leaves exactly what a store that never
  saw them holds.
- Eight threads putting and removing overlapping objects end with an empty store, and four
  writers churning the same chunks never show a reader a partial object. ThreadSanitizer
  reports no race.
- Replication sends only changed chunks, handles a digest that repeats inside one object, and
  a transfer that fails after some chunks arrived leaves the remote as it was.
- A reference count that would pass 2^32 - 1 throws and leaves the chunk unchanged.

`make mutants` makes eleven broken copies: never freeing a chunk, not pinning on the answer,
cutting at a fixed size, not releasing after a failed transfer, dropping a lock, skipping a
reference count, a wrong rotation in SHA-256, reading chunks in the wrong order, forgetting
to roll back inserted chunks, letting the count wrap, and ignoring the minimum chunk size.
All eleven are caught: ten by a failing test and one by ThreadSanitizer.

## Results

Copied from `results/summary.md`. Synthetic data: a 16 MiB random first version and nine
more, each the one before it with 24 random edits (insert or delete 1-256 bytes, or overwrite
4 KiB). Five seeds; ranges are min - max.

| | content-defined | fixed 8 KiB |
|---|---|---|
| mean chunk size, KiB | 9.13 - 9.24 | 8.00 |
| bytes written / bytes stored, 10 versions | 8.43 - 8.54 | 1.04 - 1.07 |
| bytes on the wire / object bytes, versions 2-10 | 0.0225 - 0.0242 | 0.9360 - 0.9671 |
| chunks sent per version | 31.0 - 35.6 | 1900.7 - 1964.9 |

The content-defined cut rule is set for an 8 KiB average, and the chunks it makes average a
little more, about 9 KiB, so the two columns are not at exactly the same chunk size. I did
not run the fixed baseline at other sizes.

- Ten unrelated objects store at 1.00, so the ratio comes from shared content.
- Version 1 to an empty remote costs 1.0072 - 1.0073 of its size on the wire (headers).
- After removing versions 1-5 from the source and the remote, both hold exactly the bytes of
  a store that only saw versions 6-10: 0 bytes of difference on every seed.
- Speed on one machine, 16 MiB, 5 runs: chunking alone 1460 MiB/s (1415 - 1503); chunking,
  SHA-256 and storing 172 MiB/s (170 - 177). That is the saved file, the third of three
  `make bench` runs. The other two gave 1565 and 1234 MiB/s for chunking alone and 184 and
  152 MiB/s for the full path, so the speed rows move more between runs than within one.
  The other rows were identical in all three.

## What this does not show

- The data is random bytes with edits, not real files. Real data has structure and
  compresses; the ratios say nothing about what a real system would see.
- The fixed-size result depends on the edit mix. Insertions and deletions shift every byte
  after them, which is what breaks fixed chunks. With overwrites only, fixed chunks would
  deduplicate as well.
- The wire sizes count this protocol's messages (8 bytes plus 32 per digest for the recipe,
  36 bytes plus data per chunk, one bit per digest for the answer). Nothing crossed a
  network, and there is no latency or loss.
- Everything is in memory in one process. There is no disk, no crash recovery, and no way
  to rebuild the counts after a crash.
- A recipe is a plain value. Removing the same recipe twice throws only once a chunk has no
  references left; while other objects hold the same chunks, the second remove takes their
  references. A remove that throws part-way leaves the recipe partly removed.
- The SHA-256 is plain C++, not tuned; 172 MiB/s end to end says little about a tuned one.
- Measured on one machine (WSL2, g++ 13.3, -O2). The speed numbers will differ on another.
