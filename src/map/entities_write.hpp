#pragma once

#include "map/placement.hpp"

#include <filesystem>

namespace slopengine {

bool writeMapEntities(const std::filesystem::path& path, const PlacementDocument& doc);

}
