#pragma once

#include "map/placement.hpp"

#include <filesystem>

namespace slopengine {

/** Writes a PlacementDocument as entities.s7 Scheme text. */
bool writeMapEntities(const std::filesystem::path& path, const PlacementDocument& doc);

}
