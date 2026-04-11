#include "hash_engine.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace duplicate_library {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::size_t kBufferSize = 64 * 1024;

}  // namespace

std::string HashEngine::hash_file(const std::filesystem::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::uint64_t hash = kFnvOffsetBasis;
    std::array<char, kBufferSize> buffer{};

    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= kFnvPrime;
        }
    }

    if (input.bad()) {
        return {};
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

}  // namespace duplicate_library
