#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace slopengine {

/** Command-line configuration for mounting game packages. */
struct AppConfig {
    std::filesystem::path base_game;
    std::vector<std::filesystem::path> mods;

    /** Parses @p argc/@p argv into an AppConfig, or returns nullopt on error. */
    static std::optional<AppConfig> parse(int argc, char* argv[]);

    /** Prints command-line usage to stderr. */
    static void printUsage(const char* program);
};

}
