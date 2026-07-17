#pragma once

#include "map/bsp.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

constexpr std::uint32_t kBspMagic = 0x32505342u; // "BSP2" LE
constexpr std::uint32_t kBspVersion = 2;

bool writeBspFile(const std::filesystem::path& path, const BspTree& tree);
std::optional<BspTree> readBspFile(const std::filesystem::path& path);
std::optional<BspTree> readBspBytes(std::span<const std::byte> data);

}
