#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "sha256.hpp"

using namespace dedup;

static std::string hash_of(const std::string& s) {
    return to_hex(sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

TEST(sha256_known_vectors) {
    CHECK(hash_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hash_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(hash_of(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Lengths 55, 56, 63, 64 and 65 sit on the padding boundaries where an off-by-one shows.
TEST(sha256_padding_boundaries) {
    CHECK(hash_of(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hash_of(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(hash_of(std::string(63, 'a')) ==
          "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    CHECK(hash_of(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK(hash_of(std::string(65, 'a')) ==
          "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
}
