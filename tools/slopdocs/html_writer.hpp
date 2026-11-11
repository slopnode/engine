#pragma once

#include "site.hpp"

#include <filesystem>
#include <string>

namespace slopdocs {

bool writeSite(const Site& site, const std::filesystem::path& outDir, std::string& error, bool verbose);

}
