#pragma once

#include <flecs.h>
#include <string>
#include <vector>

namespace slopengine {

struct ConsoleState {
    bool open = false;
    char inputBuffer[512]{};
    std::vector<std::string> log;
};

}