#pragma once

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
};

bool parseMapMeta(std::string_view source, MapMeta& out);

}
