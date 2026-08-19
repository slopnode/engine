#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/**
 * Resolves the path to @p exeNameNoExt (platform executable suffix appended)
 * sitting next to the currently running executable. Empty if not found.
 */
std::filesystem::path resolveSiblingExecutable(std::string_view exeNameNoExt);

/**
 * Fire-and-forget process launch: posix_spawn on POSIX, CreateProcessA on
 * Windows. @p args[0] is conventionally the program name (argv[0]
 * convention); the child's own argv[0] is taken from it, not from @p exePath.
 * Does not wait for or track the child. Returns false + @p errorOut on
 * failure to spawn.
 */
bool spawnDetached(
    const std::filesystem::path& exePath,
    const std::vector<std::string>& args,
    std::string& errorOut);

}
