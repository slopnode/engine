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
constexpr std::uint32_t kBspVersion = 4; // v4: BspPortal.doorBrushId

/** Writes @p tree as a BSP2 file. */
bool writeBspFile(const std::filesystem::path& path, const BspTree& tree);
/** Reads a BSP2 file from disk. */
std::optional<BspTree> readBspFile(const std::filesystem::path& path);
/** Reads a BSP2 blob from memory. */
std::optional<BspTree> readBspBytes(std::span<const std::byte> data);

}
