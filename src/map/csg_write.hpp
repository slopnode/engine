#pragma once

#include "map/brush.hpp"
#include "map/prefab.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

/** Writes brushes as Scheme CSG to @p path. */
bool writeMapBrushes(const std::filesystem::path& path, const std::vector<Brush>& brushes);

/** Writes brushes and prefab instances as a map CSG document. */
bool writeMapCsgDocument(
    const std::filesystem::path& path,
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances);

/** Serializes brushes to CSG text. */
std::string brushesToCsgText(const std::vector<Brush>& brushes);

/** Serializes brushes and prefab instances to CSG text. */
std::string mapCsgDocumentToText(
    const std::vector<Brush>& brushes,
    const std::vector<PrefabInstance>& instances);

}
