#pragma once

#include "core/package.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

std::filesystem::path userConfigDirectory();
std::filesystem::path userCacheDirectory();

/**
 * Path to the global settings.cfg: package search paths ([paths]) live here,
 * unscoped by profile or mounted package, because they're what resolves a
 * --base-game/--mod name in the first place -- scoping them by package would
 * make finding the package depend on already having found it.
 */
std::filesystem::path userSettingsPath();
std::filesystem::path userSlopmapSettingsPath();
std::filesystem::path userScreenshotDirectory();
std::filesystem::path userSavesDirectory();
std::filesystem::path defaultSlopmapThumbnailCacheDirectory();

/** Additional package search directories from settings.cfg's [paths] section (search_path= lines). */
std::vector<std::filesystem::path> userConfiguredSearchPaths();

/**
 * Overwrites settings.cfg's [paths] section with @p paths as search_path=
 * lines, adding the section if absent. Every other section is left
 * untouched. Returns false if the file couldn't be written.
 */
bool saveUserConfiguredSearchPaths(const std::vector<std::filesystem::path>& paths);

/**
 * Directory holding one subfolder per profile for this engine+base pair --
 * the parent of profileSettingsPath's per-profile settings.cfg. Lets a
 * caller enumerate existing profiles for a given base game.
 */
std::filesystem::path profilesRootForBase(const Package& engine, const Package& base);

/**
 * Path to the per-profile settings.cfg (graphics + controls), scoped by the
 * mounted engine and base-game package identity plus @p profile, so that
 * "default" (or any other profile name) means something different per game
 * rather than one settings file shared across every base game you own.
 */
std::filesystem::path profileSettingsPath(
    const Package& engine,
    const Package& base,
    std::string_view profile);

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
