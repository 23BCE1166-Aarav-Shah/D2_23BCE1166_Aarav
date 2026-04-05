#include "duplicate_finder/hash_engine.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

namespace duplicate_library {
namespace {

TEST(HashEngineTest, ComputesStableHashForIdenticalFiles) {
    test::ScopedTempDir temp_dir;
    const auto file_a = temp_dir.path() / "a.bin";
    const auto file_b = temp_dir.path() / "b.bin";

    test::write_file(file_a, "same-content-12345");
    test::write_file(file_b, "same-content-12345");

    HashEngine hash_engine;
    const auto hash_a = hash_engine.compute_xxhash(file_a);
    const auto hash_b = hash_engine.compute_xxhash(file_b);

    EXPECT_FALSE(hash_a.empty());
    EXPECT_EQ(hash_a, hash_b);
}

TEST(HashEngineTest, ProducesDifferentHashesForDifferentFiles) {
    test::ScopedTempDir temp_dir;
    const auto file_a = temp_dir.path() / "a.bin";
    const auto file_b = temp_dir.path() / "b.bin";

    test::write_file(file_a, "alpha");
    test::write_file(file_b, "beta");

    HashEngine hash_engine;
    EXPECT_NE(hash_engine.compute_xxhash(file_a), hash_engine.compute_xxhash(file_b));
}

TEST(HashEngineTest, ReturnsEmptyHashForMissingFile) {
    HashEngine hash_engine;
    EXPECT_TRUE(hash_engine.compute_xxhash("/definitely/missing/file.bin").empty());
}

}  // namespace
}  // namespace duplicate_library
