#pragma once

#include "map/thing.hpp"

#include <filesystem>

namespace slopengine {

bool writeMapThings(const std::filesystem::path& path, const ThingDocument& doc);

}
