#pragma once

#include "map/vis.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace slopengine {

constexpr std::uint32_t kVisMagic = 0x31534956u; // "VIS1" LE
constexpr std::uint32_t kVisVersion = 2;

bool writeVisFile(const std::filesystem::path& path, const VisFile& vis);
std::optional<VisFile> readVisFile(const std::filesystem::path& path);
std::optional<VisFile> readVisBytes(std::span<const std::byte> data);

}
