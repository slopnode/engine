#pragma once

#include "map/pvs.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace slopengine {

constexpr std::uint32_t kPvsMagic = 0x31535650u; // "PVS1" LE
constexpr std::uint32_t kPvsVersion = 1;

bool writePvsFile(const std::filesystem::path& path, const PvsFile& pvs);
std::optional<PvsFile> readPvsFile(const std::filesystem::path& path);
std::optional<PvsFile> readPvsBytes(std::span<const std::byte> data);

}
