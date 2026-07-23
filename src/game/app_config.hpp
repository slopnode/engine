#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

enum class PackageCliValueKind {
    Flag,
    String,
};

struct PackageCliFlag {
    std::string name;
    PackageCliValueKind kind = PackageCliValueKind::String;
    std::string help;
};

/** Command-line configuration for mounting game packages and package flags. */
struct AppConfig {
    std::string programName = "slopengine";
    std::filesystem::path base_game;
    std::vector<std::filesystem::path> mods;
    /** Tool-facing map id (`slopbsp` / `slopfac` / `slopvis` / editors). Not used by the game runtime. */
    std::optional<std::string> map;
    std::vector<std::string> pendingArgs;
    std::unordered_map<std::string, std::string> packageArgs;

    /** Parses only `--base-game` / `--mod`; other tokens go to pendingArgs. */
    static std::optional<AppConfig> parseMount(int argc, char* argv[]);

    /**
     * Tool helper: mount flags plus optional `--map`.
     * Consumes `--map` into map; other unknown tokens fail.
     */
    static std::optional<AppConfig> parse(int argc, char* argv[]);

    /** Parses pendingArgs against @p schema into packageArgs. */
    bool parsePackageArgs(const std::vector<PackageCliFlag>& schema, std::string& errorOut);

    /** Prints mount usage, plus package flag help when @p schema is non-empty. */
    static void printUsage(const char* program, const std::vector<PackageCliFlag>& schema = {});
};

}
