#pragma once

#include <raylib.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Fields from maps/<name>/map.meta. */
struct MapMeta {
    std::string id;
    std::string name;
    std::string package; /**< Owning package id. */
    std::vector<std::string> depends;
    Vector3 ambient{0.02f, 0.02f, 0.025f}; /**< Soft fill used when baking radiosity. */
};

/** Parses map.meta text into @p out. */
bool parseMapMeta(std::string_view source, MapMeta& out);

}
