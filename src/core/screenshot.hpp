#pragma once

#include <filesystem>
#include <string>

namespace slopengine {

bool saveScreenshotPng(std::filesystem::path& outPath, std::string& outError);

}
