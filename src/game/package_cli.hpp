#pragma once

#include "game/app_config.hpp"

#include <vector>

struct s7_scheme;

namespace slopengine {

/** Reads *package-cli* from Scheme into flag definitions. Missing/empty → empty schema. */
std::vector<PackageCliFlag> parsePackageCliFromScheme(s7_scheme* scheme);

}
