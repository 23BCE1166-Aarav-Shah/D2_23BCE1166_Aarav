#include "duplicate_finder/duplicate_engine.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace duplicate_library {
namespace {

std::vector<DuplicateGroup> normalize_groups(std::vector<DuplicateGroup> groups) {
    for (auto& group : groups) {
        std::sort(group.files.begin(), group.files.end());
    }

    std::sort(groups.begin(), groups.end(), [](const DuplicateGroup& lhs, const DuplicateGroup& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
        }
        return lhs.files < rhs.files;
    });

    return groups;
}

TEST(DuplicateEngineTest, GroupsExactBinaryDuplicates) {
    test::ScopedTempDir temp_dir;
    test::ScopedEnvVar cache_home("XDG_CACHE_HOME", (temp_dir.path() / "cache").string());

    const auto file_a = temp_dir.path() / "one.txt";
    const auto file_b = temp_dir.path() / "nested" / "two.txt";
    const auto file_c = temp_dir.path() / "three.txt";

    test::write_file(file_a, "duplicate-payload");
    test::write_file(file_b, "duplicate-payload");
    test::write_file(file_c, "unique-payload");

    DuplicateEngine engine(2);
    auto groups = normalize_groups(engine.scan(temp_dir.path().string()));

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.front().type, "binary");
    EXPECT_EQ(groups.front().files, std::vector<std::string>({
        file_a.lexically_normal().string(),
        file_b.lexically_normal().string(),
    }));
}

TEST(DuplicateEngineTest, DoesNotGroupFilesWithSameSizeButDifferentContent) {
    test::ScopedTempDir temp_dir;
    test::ScopedEnvVar cache_home("XDG_CACHE_HOME", (temp_dir.path() / "cache").string());

    const auto file_a = temp_dir.path() / "a.bin";
    const auto file_b = temp_dir.path() / "b.bin";

    test::write_file(file_a, "abcde");
    test::write_file(file_b, "12345");

    DuplicateEngine engine(2);
    EXPECT_TRUE(engine.scan(temp_dir.path().string()).empty());
}

TEST(DuplicateEngineTest, ReturnsEmptyForMissingDirectory) {
    test::ScopedTempDir temp_dir;
    test::ScopedEnvVar cache_home("XDG_CACHE_HOME", (temp_dir.path() / "cache").string());

    DuplicateEngine engine;
    EXPECT_TRUE(engine.scan((temp_dir.path() / "missing").string()).empty());
}

TEST(DuplicateEngineTest, ReportsProgressDeterministically) {
    test::ScopedTempDir temp_dir;
    test::ScopedEnvVar cache_home("XDG_CACHE_HOME", (temp_dir.path() / "cache").string());

    test::write_file(temp_dir.path() / "a.txt", "same");
    test::write_file(temp_dir.path() / "b.txt", "same");
    test::write_file(temp_dir.path() / "c.txt", "diff");

    DuplicateEngine engine(2);
    std::vector<int> progress_updates;
    engine.setProgressCallback([&](int progress) {
        progress_updates.push_back(progress);
    });

    const auto groups = engine.scan(temp_dir.path().string());

    ASSERT_FALSE(progress_updates.empty());
    EXPECT_EQ(progress_updates.front(), 0);
    EXPECT_EQ(progress_updates.back(), 100);
    EXPECT_EQ(groups.size(), 1u);
}

}  // namespace
}  // namespace duplicate_library
