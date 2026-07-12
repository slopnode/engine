#include "game/app.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    const auto config = slopengine::AppConfig::parse(argc, argv);
    if (!config) {
        slopengine::AppConfig::printUsage(argv[0]);
        return 1;
    }

    try {
        slopengine::App app{*config};
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
