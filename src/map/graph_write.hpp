#pragma once

#include "map/graph.hpp"

#include <filesystem>
#include <string>

namespace slopengine {

std::string formatMapGraphs(const GraphDocument& doc);
bool writeMapGraphs(const std::filesystem::path& path, const GraphDocument& doc);

}
