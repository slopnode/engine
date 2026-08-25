#pragma once

#include "core/package.hpp"
#include "game/app_config.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sloplauncher {

/** All non-UI launcher state: discovered packages, current selections, and the launch action. */
struct LauncherState {
    std::vector<std::filesystem::path> searchPaths;
    std::string newSearchPathInput;
    std::string searchPathError;

    std::vector<slopengine::Package> discovered;
    std::string packageFilterText;

    /**
     * The engine package (resolved separately from searchPaths via
     * resolveEnginePackage(), the same way the real slopengine binary finds
     * it) so dependency checks on e.g. "slopengine.engine" resolve correctly
     * instead of reporting the engine as missing.
     */
    std::optional<slopengine::Package> enginePackage;

    /** Package to show in the "Package" details tab; set by clicking a row in the list. */
    std::string selectedPackageId;

    std::string baseGameId;
    std::vector<std::string> modIds;

    /** Package (base game or one of modIds) that dev-tool saves go to; must stay one of those. */
    std::string devTargetId;

    std::vector<std::string> existingProfiles;
    std::string profileName = "default";

    std::vector<slopengine::PackageCliFlag> cliSchema;
    std::unordered_map<std::string, std::string> cliStringValues;
    std::unordered_map<std::string, bool> cliFlagValues;

    bool debugMode = false;
    bool verboseMode = false;
    std::string statusMessage;
    bool statusIsError = false;

    /** Seeds searchPaths from settings.cfg and does the first package scan. */
    void init();

    /** Re-scans all search paths; drops a selected base game/mod that vanished. */
    void refreshPackages();

    /** Validates @p dir, appends it, persists to settings.cfg, and rescans. */
    bool addSearchPath(const std::string& dir);
    void removeSearchPath(std::size_t index);

    /** Selects @p id as the base game, refreshing profiles and cli.s7 options for it. */
    void setBaseGame(const std::string& id);
    void toggleMod(const std::string& id);

    const slopengine::Package* findPackage(const std::string& id) const;

    /** Base game (if any) followed by mods, in mount order. */
    std::vector<std::string> mountedPackageIds() const;

    /** argv (args[0] = program name) matching what AppConfig::parse expects. */
    std::vector<std::string> buildLaunchArgs() const;

    /** Resolves the sibling slopengine binary and spawns it detached. */
    void launch();

    /**
     * Launches a sibling dev tool ("slopmap" / "slopsprite" / "slopthing")
     * with --base-game/--mod/--target, mirroring how those tools are already
     * invoked by hand: --target must be baseGameId or one of modIds.
     */
    void launchDevTool(const std::string& toolName);

private:
    void refreshProfiles();
    void refreshCliSchema();
    void refreshDevTarget();
};

}
