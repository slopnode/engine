#include "game/app_config.hpp"

#include <iostream>

namespace slopengine {

void AppConfig::printUsage(const char* program) {
    std::cerr
        << "Usage: " << program << " --base-game <path> [--mod <path>]... [--map <name>]\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --map         Map folder name under maps/ (loads maps/<name>/static.csg)\n";
}

std::optional<AppConfig> AppConfig::parse(int argc, char* argv[]) {
    AppConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--base-game") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.base_game = argv[++i];
            continue;
        }

        if (arg == "--mod") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.mods.push_back(argv[++i]);
            continue;
        }

        if (arg == "--map") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.map = argv[++i];
            continue;
        }

        return std::nullopt;
    }

    if (config.base_game.empty()) {
        return std::nullopt;
    }

    return config;
}

}
