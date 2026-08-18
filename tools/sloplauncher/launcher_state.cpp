#include "launcher_state.hpp"

#include "core/engine_package.hpp"
#include "core/package_discovery.hpp"
#include "core/package_search.hpp"
#include "core/process_launch.hpp"
#include "core/user_paths.hpp"
#include "game/package_cli.hpp"

#include <s7.h>

#include <algorithm>
#include <system_error>

namespace sloplauncher {

void LauncherState::init() {
    searchPaths = slopengine::userConfiguredSearchPaths();
    refreshPackages();
}

void LauncherState::refreshPackages() {
    discovered = slopengine::discoverPackages(slopengine::applicationSearchPaths(searchPaths));

    if (!baseGameId.empty() && findPackage(baseGameId) == nullptr) {
        baseGameId.clear();
        existingProfiles.clear();
        cliSchema.clear();
        cliStringValues.clear();
        cliFlagValues.clear();
    }

    std::vector<std::string> keptMods;
    keptMods.reserve(modIds.size());
    for (const std::string& id : modIds) {
        if (findPackage(id) != nullptr) {
            keptMods.push_back(id);
        }
    }
    modIds = std::move(keptMods);

    refreshDevTarget();
}

bool LauncherState::addSearchPath(const std::string& dirInput) {
    searchPathError.clear();
    if (dirInput.empty()) {
        return false;
    }

    const std::filesystem::path dir{dirInput};
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        searchPathError = "Not a directory: " + dirInput;
        return false;
    }
    for (const std::filesystem::path& existing : searchPaths) {
        if (existing == dir) {
            searchPathError = "Already added";
            return false;
        }
    }

    searchPaths.push_back(dir);
    if (!slopengine::saveUserConfiguredSearchPaths(searchPaths)) {
        searchPathError = "Failed to write settings.cfg";
    }
    refreshPackages();
    return true;
}

void LauncherState::removeSearchPath(std::size_t index) {
    if (index >= searchPaths.size()) {
        return;
    }
    searchPaths.erase(searchPaths.begin() + static_cast<std::ptrdiff_t>(index));
    slopengine::saveUserConfiguredSearchPaths(searchPaths);
    refreshPackages();
}

void LauncherState::setBaseGame(const std::string& id) {
    if (baseGameId == id) {
        return;
    }
    baseGameId = id;
    const auto it = std::find(modIds.begin(), modIds.end(), id);
    if (it != modIds.end()) {
        modIds.erase(it);
    }
    profileName = "default";
    refreshProfiles();
    refreshCliSchema();
    refreshDevTarget();
}

void LauncherState::toggleMod(const std::string& id) {
    if (id == baseGameId) {
        return;
    }
    const auto it = std::find(modIds.begin(), modIds.end(), id);
    if (it != modIds.end()) {
        modIds.erase(it);
    } else {
        modIds.push_back(id);
    }
    refreshDevTarget();
}

const slopengine::Package* LauncherState::findPackage(const std::string& id) const {
    for (const slopengine::Package& package : discovered) {
        if (package.meta().id == id) {
            return &package;
        }
    }
    return nullptr;
}

std::vector<std::string> LauncherState::mountedPackageIds() const {
    std::vector<std::string> ids;
    if (!baseGameId.empty()) {
        ids.push_back(baseGameId);
    }
    ids.insert(ids.end(), modIds.begin(), modIds.end());
    return ids;
}

void LauncherState::refreshDevTarget() {
    const std::vector<std::string> mounted = mountedPackageIds();
    if (std::find(mounted.begin(), mounted.end(), devTargetId) == mounted.end()) {
        devTargetId = baseGameId;
    }
}

void LauncherState::refreshProfiles() {
    existingProfiles.clear();

    const slopengine::Package* base = findPackage(baseGameId);
    if (base == nullptr) {
        return;
    }
    const auto enginePath = slopengine::resolveEnginePackage();
    if (!enginePath) {
        return;
    }
    const slopengine::Package engine{*enginePath};
    const std::filesystem::path root = slopengine::profilesRootForBase(engine, *base);

    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec);
    if (ec) {
        return;
    }
    for (const std::filesystem::directory_entry& entry : it) {
        std::error_code isDirEc;
        if (entry.is_directory(isDirEc) && !isDirEc) {
            existingProfiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(existingProfiles.begin(), existingProfiles.end());
}

void LauncherState::refreshCliSchema() {
    cliSchema.clear();
    cliStringValues.clear();
    cliFlagValues.clear();

    const slopengine::Package* base = findPackage(baseGameId);
    if (base == nullptr) {
        return;
    }
    const std::filesystem::path cliPath = base->root() / "data" / "cli.s7";
    std::error_code ec;
    if (!std::filesystem::exists(cliPath, ec)) {
        return;
    }

    s7_scheme* scheme = s7_init();
    if (scheme == nullptr) {
        return;
    }
    s7_load(scheme, cliPath.string().c_str());
    cliSchema = slopengine::parsePackageCliFromScheme(scheme);
    s7_quit(scheme);

    for (const slopengine::PackageCliFlag& flag : cliSchema) {
        if (flag.kind == slopengine::PackageCliValueKind::Flag) {
            cliFlagValues[flag.name] = false;
        } else {
            cliStringValues[flag.name] = "";
        }
    }
}

std::vector<std::string> LauncherState::buildLaunchArgs() const {
    std::vector<std::string> args{"slopengine"};

    const slopengine::Package* base = findPackage(baseGameId);
    if (base == nullptr) {
        return args;
    }
    args.push_back("--base-game");
    args.push_back(base->root().string());

    for (const std::string& modId : modIds) {
        const slopengine::Package* mod = findPackage(modId);
        if (mod == nullptr) {
            continue;
        }
        args.push_back("--mod");
        args.push_back(mod->root().string());
    }

    args.push_back("--profile");
    args.push_back(profileName.empty() ? "default" : profileName);

    if (debugMode) {
        args.push_back("--debug");
    }

    for (const slopengine::PackageCliFlag& flag : cliSchema) {
        if (flag.kind == slopengine::PackageCliValueKind::Flag) {
            const auto it = cliFlagValues.find(flag.name);
            if (it != cliFlagValues.end() && it->second) {
                args.push_back("--" + flag.name);
            }
        } else {
            const auto it = cliStringValues.find(flag.name);
            if (it != cliStringValues.end() && !it->second.empty()) {
                args.push_back("--" + flag.name);
                args.push_back(it->second);
            }
        }
    }

    return args;
}

void LauncherState::launch() {
    statusMessage.clear();
    statusIsError = false;

    if (baseGameId.empty()) {
        statusMessage = "Select a base game first";
        statusIsError = true;
        return;
    }

    const std::filesystem::path exe = slopengine::resolveSiblingExecutable("slopengine");
    if (exe.empty()) {
        statusMessage = "slopengine not found next to sloplauncher";
        statusIsError = true;
        return;
    }

    std::string errorOut;
    if (!slopengine::spawnDetached(exe, buildLaunchArgs(), errorOut)) {
        statusMessage = errorOut;
        statusIsError = true;
        return;
    }

    statusMessage = "Launched slopengine";
    statusIsError = false;
}

void LauncherState::launchDevTool(const std::string& toolName) {
    statusMessage.clear();
    statusIsError = false;

    const slopengine::Package* base = findPackage(baseGameId);
    if (base == nullptr) {
        statusMessage = "Select a base game first";
        statusIsError = true;
        return;
    }
    const slopengine::Package* target = findPackage(devTargetId);
    if (target == nullptr) {
        statusMessage = "Select a target package first";
        statusIsError = true;
        return;
    }

    const std::filesystem::path exe = slopengine::resolveSiblingExecutable(toolName);
    if (exe.empty()) {
        statusMessage = toolName + " not found next to sloplauncher";
        statusIsError = true;
        return;
    }

    std::vector<std::string> args{toolName, "--base-game", base->root().string()};
    for (const std::string& modId : modIds) {
        const slopengine::Package* mod = findPackage(modId);
        if (mod == nullptr) {
            continue;
        }
        args.push_back("--mod");
        args.push_back(mod->root().string());
    }
    args.push_back("--target");
    args.push_back(target->root().string());

    std::string errorOut;
    if (!slopengine::spawnDetached(exe, args, errorOut)) {
        statusMessage = errorOut;
        statusIsError = true;
        return;
    }

    statusMessage = "Launched " + toolName;
    statusIsError = false;
}

}
