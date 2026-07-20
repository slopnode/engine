#pragma once

#include <filesystem>
#include <optional>

namespace slopengine {

std::optional<std::filesystem::path> resolveEnginePackage();

}
