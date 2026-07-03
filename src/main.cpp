#include "game/app.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    const auto config = daggerlike::AppConfig::parse(argc, argv);
    if (!config) {
        daggerlike::AppConfig::printUsage(argv[0]);
        return 1;
    }

    try {
        daggerlike::App app{*config};
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
