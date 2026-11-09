#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <thread>
#include <utility>
#include <vector>

#include "check.hpp"
#include "store.hpp"
#include "synth.hpp"

using namespace dedup;

std::vector<std::vector<std::uint8_t>> make_pool();  // test_model.cpp

// Eight threads put and remove overlapping objects. Removing a chunk's last reference while
// another thread adds one is the case a missing lock would break.
TEST(concurrent_puts_and_removes_leave_the_store_empty) {
    auto pool = make_pool();
    ChunkStore store;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            try {
                synth::Rng rng(1000 + t);
                std::deque<Recipe> mine;
                for (int i = 0; i < 25; ++i) {
                    std::size_t idx = rng.below(pool.size());
                    Recipe r = store.put(pool[idx]);
                    if (i % 5 == 0) CHECK(store.get(r) == pool[idx]);
                    mine.push_back(std::move(r));
                    if (mine.size() > 3) {
                        store.remove(mine.front());
                        mine.pop_front();
                    }
                }
                for (const Recipe& r : mine) store.remove(r);
            } catch (const std::exception&) {
                CHECK(false);
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK(store.stored_bytes() == 0);
    CHECK(store.chunk_count() == 0);
}

// An object that stays stored must read back whole while other threads churn the same chunks.
TEST(concurrent_readers_see_whole_objects_while_writers_churn) {
    auto pool = make_pool();
    ChunkStore store;
    Recipe held = store.put(pool[0]);
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            try {
                synth::Rng rng(2000 + t);
                for (int i = 0; i < 25; ++i) {
                    Recipe r = store.put(pool[rng.below(pool.size())]);
                    store.remove(r);
                }
            } catch (const std::exception&) {
                CHECK(false);
            }
        });
    }
    int reads = 0;
    std::thread reader([&] {
        try {
            do {
                CHECK(store.get(held) == pool[0]);
                ++reads;
            } while (!stop);
        } catch (const std::exception&) {
            CHECK(false);
        }
    });
    for (auto& th : threads) th.join();
    stop = true;
    reader.join();
    CHECK(reads >= 1);
    CHECK(store.audit({held}).empty());
}
