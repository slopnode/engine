#pragma once

#include <filesystem>

namespace slopengine {

std::filesystem::path userConfigDirectory();
std::filesystem::path userSettingsPath();
std::filesystem::path userScreenshotDirectory();

}
