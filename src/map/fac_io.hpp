#pragma once

#include "map/fac.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace slopengine {

constexpr std::uint32_t kFacMagic = 0x31434146u; // "FAC1" LE
constexpr std::uint32_t kFacVersion = 3;

bool writeFacFile(const std::filesystem::path& path, const FacFile& fac);
std::optional<FacFile> readFacFile(const std::filesystem::path& path);
std::optional<FacFile> readFacBytes(std::span<const std::byte> data);

}
