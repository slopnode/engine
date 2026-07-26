#pragma once

#include "core/package.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

std::filesystem::path userConfigDirectory();
std::filesystem::path userCacheDirectory();
std::filesystem::path userSettingsPath();
std::filesystem::path userSlopmapSettingsPath();
std::filesystem::path userScreenshotDirectory();
std::filesystem::path userSavesDirectory();
std::filesystem::path defaultSlopmapThumbnailCacheDirectory();

/** Sanitizes a single path segment for use under the saves tree. */
std::string sanitizeSaveSegment(std::string_view text);

/**
 * Save context root for the mounted stack: engine -> base -> mods (CLI order).
 * packages[0] = engine, packages[1] = base, packages[2..] = mods.
 */
std::filesystem::path buildSaveContextRoot(const std::vector<Package>& packages);

/** Writes mount.s7 under @p contextRoot if missing (ids, versions, roots). */
bool ensureSaveMountSidecar(
    const std::filesystem::path& contextRoot,
    const std::vector<Package>& packages);

/**
 * Resolves @p relative under @p contextRoot. Rejects empty, absolute, and
 * paths that escape the root via "..".
 */
bool resolveSaveRelativePath(
    const std::filesystem::path& contextRoot,
    std::string_view relative,
    std::filesystem::path& outAbsolute);

}
