#pragma once

#include "map/nav_graph.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace slopengine {

constexpr std::uint32_t kNavMagic = 0x3156414Eu; // "NAV1" LE
constexpr std::uint32_t kNavVersion = 3; // v3 adds per-link climbHeight (NavPortalLink)

// Only MapNavigation::adjacency is stored; reverseAdjacency is rebuilt after load the
// same way every builder (buildMapNavigation, buildMapNavigationFromPolyMesh) already
// derives it from adjacency, so there's no reason to double the file size storing both.
bool writeNavFile(const std::filesystem::path& path, const MapNavigation& nav);
std::optional<MapNavigation> readNavFile(const std::filesystem::path& path);
std::optional<MapNavigation> readNavBytes(std::span<const std::byte> data);

}
