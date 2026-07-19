#pragma once

#include "map/brush.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

bool writeMapBrushes(const std::filesystem::path& path, const std::vector<Brush>& brushes);

std::string brushesToCsgText(const std::vector<Brush>& brushes);

}
