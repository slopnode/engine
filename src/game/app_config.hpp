#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace daggerlike {

struct AppConfig {
    std::filesystem::path base_game;
    std::vector<std::filesystem::path> mods;

    static std::optional<AppConfig> parse(int argc, char* argv[]);
    static void printUsage(const char* program);
};

}
