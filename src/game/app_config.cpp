#include "game/app_config.hpp"

#include "core/engine_version.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace slopengine {

namespace {

std::string programDisplayName(const char* program) {
    return std::filesystem::path(program).stem().string();
}

}

void AppConfig::printVersion(const char* program) {
    std::cout << programDisplayName(program) << " - v" << kSlopengineVersion << "\n";
}

void AppConfig::printUsage(const char* program, const std::vector<PackageCliFlag>& schema) {
    std::cerr << programDisplayName(program) << " - v" << kSlopengineVersion << "\n\n";
    std::cerr << "Usage: " << program
              << " --game <path|name> [--mod <path|name>]... [--profile <name>] [package-flags...]\n"
              << "\n"
              << "  --game        Game package: a directory path, or a name looked up in\n"
              << "                the configured search paths (required)\n"
              << "  --mod         Additional mod package: path or name, same lookup as\n"
              << "                --game (repeatable)\n"
              << "  --profile     Settings/saves/screenshots profile to use (default: \"default\")\n"
              << "  --debug       Enable developer-only UI (e.g. the Debug menu)\n"
              << "  --verbose     Raise the trace log level to show DEBUG messages\n"
              << "  --perf-log    Log averaged frame timings and counters every two seconds\n"
              << "  --perf-view   Lock the camera at x,y,z,yaw,pitch for repeatable measurements\n"
              << "  --dedicated   Run the simulation only, with no window, GPU, audio,\n"
              << "                drawing or local input\n"
              << "  --listen      Host a game on port or host:port\n"
              << "  --connect     Join a game at host:port, or with --dedicated join without a window\n"
              << "  --server-key  With --connect, refuse servers whose key isn't this 64-digit hex key\n"
              << "  --session     With --listen, the session kind from data/net.s7 to host\n"
              << "  --max-players With --listen, allow fewer players than the session kind does\n"
              << "  --bot         With --dedicated --connect, play by running scripts/bots/NAME.s7 or a .s7 file\n"
              << "  --net-lag     Delay everything this process sends by this many milliseconds\n"
              << "  --net-jitter  Vary that delay by up to this many milliseconds either way\n"
              << "  --net-loss    Drop this fraction, 0 to 1, of the unreliable messages it sends\n"
              << "  --net-stats   Log network statistics every this many seconds\n"
              << "  --net-max-rewind With --listen, move targets back at most this many milliseconds for a hit, 0 for never\n"
              << "  --net-peers-per-address With --listen, allow this many connections from one address, 4 by default\n"
              << "  --version     Print the engine version and exit\n";
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

        if (arg == "--game") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.game = argv[++i];
            continue;
        }

        if (arg == "--mod") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            config.mods.push_back(argv[++i]);
            continue;
        }

        if (arg == "--version") {
            printVersion(config.programName.c_str());
            std::exit(0);
        }

        if (arg == "--debug") {
            config.debug = true;
            continue;
        }

        if (arg == "--verbose") {
            config.verbose = true;
            continue;
        }

        if (arg == "--perf-log") {
            config.perfLog = true;
            continue;
        }

        if (arg == "--dedicated") {
            config.dedicated = true;
            continue;
        }

        if (arg == "--max-players" || arg == "--net-peers-per-address") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            char* end = nullptr;
            const long value = std::strtol(argv[++i], &end, 10);
            if (end == nullptr || *end != '\0' || value < 1) {
                return std::nullopt;
            }
            int& target = arg == "--max-players" ? config.maxPlayers : config.netPeersPerAddress;
            target = static_cast<int>(std::min<long>(value, 1 << 20));
            continue;
        }

        if (arg == "--net-lag" || arg == "--net-jitter" || arg == "--net-loss" || arg == "--net-stats" ||
            arg == "--net-max-rewind") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            char* end = nullptr;
            const float value = std::strtof(argv[++i], &end);
            const float limit = arg == "--net-loss" ? 1.0f : 10000.0f;
            if (end == nullptr || *end != '\0' || !(value >= 0.0f) || value > limit) {
                return std::nullopt;
            }
            float& target = arg == "--net-lag" ? config.netLagMs
                : arg == "--net-jitter"        ? config.netJitterMs
                : arg == "--net-loss"          ? config.netLoss
                : arg == "--net-max-rewind"    ? config.netMaxRewindMs
                                               : config.netStatsSeconds;
            target = value;
            continue;
        }

        if (arg == "--listen" || arg == "--connect" || arg == "--server-key" || arg == "--session" || arg == "--bot") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            std::string& target = arg == "--listen" ? config.listenAddress
                : arg == "--connect"                ? config.connectAddress
                : arg == "--session"                ? config.sessionKind
                : arg == "--bot"                    ? config.bot
                                                    : config.serverKey;
            target = argv[++i];
            continue;
        }

        if (arg == "--perf-view") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            if (std::sscanf(
                    argv[++i],
                    "%f,%f,%f,%f,%f",
                    &config.perfView[0],
                    &config.perfView[1],
                    &config.perfView[2],
                    &config.perfView[3],
                    &config.perfView[4]) != 5) {
                return std::nullopt;
            }
            config.perfViewLock = true;
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

    if (config.game.empty()) {
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
