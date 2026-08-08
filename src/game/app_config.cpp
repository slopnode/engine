#include "game/app_config.hpp"

#include <iostream>

namespace slopengine {

void AppConfig::printUsage(const char* program, const std::vector<PackageCliFlag>& schema) {
    std::cerr << "Usage: " << program
              << " --base-game <path|name> [--mod <path|name>]... [--profile <name>] [package-flags...]\n"
              << "\n"
              << "  --base-game   Base game package: a directory path, or a name looked up in\n"
              << "                the configured search paths (required)\n"
              << "  --mod         Additional mod package: path or name, same lookup as\n"
              << "                --base-game (repeatable)\n"
              << "  --profile     Settings/saves/screenshots profile to use (default: \"default\")\n";
    if (schema.empty()) {
        std::cerr << "\nPackage flags are declared in the base game's data/cli.s7.\n";
        return;
    }
    std::cerr << "\nPackage flags:\n";
    for (const PackageCliFlag& flag : schema) {
        std::cerr << "  --" << flag.name;
        if (flag.kind == PackageCliValueKind::String) {
            std::cerr << " <value>";
        }
        if (!flag.help.empty()) {
            std::cerr << "  " << flag.help;
        }
        std::cerr << "\n";
    }
}

std::optional<AppConfig> AppConfig::parseMount(int argc, char* argv[]) {
    AppConfig config;
    if (argc > 0 && argv[0] != nullptr) {
        config.programName = argv[0];
    }

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

        if (arg == "--profile") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.profile = argv[++i];
            if (config.profile.empty()) {
                return std::nullopt;
            }
            continue;
        }

        config.pendingArgs.push_back(arg);
    }

    if (config.base_game.empty()) {
        return std::nullopt;
    }

    return config;
}

std::optional<AppConfig> AppConfig::parse(int argc, char* argv[]) {
    auto config = parseMount(argc, argv);
    if (!config) {
        return std::nullopt;
    }

    std::vector<std::string> remaining;
    remaining.reserve(config->pendingArgs.size());
    for (std::size_t i = 0; i < config->pendingArgs.size(); ++i) {
        const std::string& token = config->pendingArgs[i];
        if (token == "--map") {
            if (i + 1 >= config->pendingArgs.size()) {
                return std::nullopt;
            }
            config->map = config->pendingArgs[++i];
            continue;
        }
        remaining.push_back(token);
    }
    if (!remaining.empty()) {
        return std::nullopt;
    }
    config->pendingArgs.clear();
    return config;
}

bool AppConfig::parsePackageArgs(
    const std::vector<PackageCliFlag>& schema,
    std::string& errorOut) {
    packageArgs.clear();

    std::unordered_map<std::string, const PackageCliFlag*> byName;
    byName.reserve(schema.size());
    for (const PackageCliFlag& flag : schema) {
        byName[flag.name] = &flag;
    }

    for (std::size_t i = 0; i < pendingArgs.size(); ++i) {
        const std::string& token = pendingArgs[i];
        if (token.rfind("--", 0) != 0 || token.size() <= 2) {
            errorOut = "unexpected argument: " + token;
            return false;
        }

        const std::string name = token.substr(2);
        const auto it = byName.find(name);
        if (it == byName.end()) {
            errorOut = "unknown flag: --" + name;
            return false;
        }

        const PackageCliFlag& flag = *it->second;
        if (flag.kind == PackageCliValueKind::Flag) {
            packageArgs[name] = "#t";
            continue;
        }

        if (i + 1 >= pendingArgs.size()) {
            errorOut = "missing value for --" + name;
            return false;
        }
        packageArgs[name] = pendingArgs[++i];
    }

    pendingArgs.clear();
    return true;
}

}
