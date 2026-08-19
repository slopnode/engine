#pragma once

#include "core/package.hpp"

#include <filesystem>
#include <vector>

namespace slopengine {

/**
 * Scans the immediate subdirectories of each root in @p searchPaths for
 * package.meta files and returns one Package per match, roots scanned in
 * order (matching applicationSearchPaths priority). When the same package id
 * appears under more than one root, only the first occurrence is kept,
 * mirroring resolveApplicationPackagePath's "first match wins" semantics.
 *
 * Role is left at the Package default; assigning engine/base/mod is left to
 * the caller as UI state, since that split isn't recorded in package.meta.
 */
std::vector<Package> discoverPackages(const std::vector<std::filesystem::path>& searchPaths);

}
