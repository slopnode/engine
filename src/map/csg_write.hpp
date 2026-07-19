#pragma once

#include "map/brush.hpp"
#include "map/prefab.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

bool writeMapBrushes(const std::filesystem::path& path, const std::vector<Brush>& brushes);

bool writeMapCsgDocument(
    const std::filesystem::path& path,
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances);

std::string brushesToCsgText(const std::vector<Brush>& brushes);

std::string mapCsgDocumentToText(
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances);

}
