#pragma once

#include <filesystem>
#include <vector>

namespace slopengine {

/** Build-configured default directories to search for game/mod packages by name. */
std::vector<std::filesystem::path> defaultApplicationSearchPaths();

/** @p userPaths (highest priority) followed by the build-configured defaults. */
std::vector<std::filesystem::path> applicationSearchPaths(
    const std::vector<std::filesystem::path>& userPaths);

/**
 * Resolves @p nameOrPath to a package root directory to try mounting.
 *
 * A literal path (relative to the current directory, or absolute) that
 * contains package.meta is used as-is. Otherwise @p nameOrPath is treated as
 * a package name and looked up as <root>/<nameOrPath> under each of
 * @p searchPaths in order; the first match wins. When nothing matches,
 * @p nameOrPath is returned unchanged so a caller's own "not found" error
 * still reports the value the user actually supplied.
 */
std::filesystem::path resolveApplicationPackagePath(
    const std::filesystem::path& nameOrPath,
    const std::vector<std::filesystem::path>& searchPaths);

}
