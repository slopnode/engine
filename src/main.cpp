#include "game/app.hpp"
#include "game/app_config.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    const auto config = slopengine::AppConfig::parseMount(argc, argv);
    if (!config) {
        slopengine::AppConfig::printUsage(argc > 0 ? argv[0] : "slopengine");
        return 1;
    }

    try {
        slopengine::App app{*config};
        return app.run();
    } catch (const std::invalid_argument& e) {
        std::cerr << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
