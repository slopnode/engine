#pragma once

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

struct MapMeta {
    std::string id;
    std::string name;
    std::string package;
    std::vector<std::string> depends;
    Vector3 ambient{0.02f, 0.02f, 0.025f};
};

bool parseMapMeta(std::string_view source, MapMeta& out);

}
