#pragma once

#include <set>
#include <string>
#include <vector>

namespace slopengine {
class AssetStore;
}

namespace slopthing {

/**
 * One thing-def property the engine exposes to Scheme via a `thing-def-*`
 * read accessor (src/script/thing_script.cpp). The engine itself never acts
 * on these — only a game's own scripts do, if they call the accessor.
 */
struct ConventionField {
    std::string accessor;
    std::string block; // "" for a top-level scalar, else a nested block key
    std::string key;
    enum class Type { Float, Int, String } type;
    std::string label;
};

const std::vector<ConventionField>& conventionFields();

/**
 * Scans every mounted package's .s7 files for references to the accessor
 * names in conventionFields(), returning whichever ones actually appear
 * somewhere. A plain text search — it won't catch an accessor built and
 * called indirectly (via string->symbol/eval), but that pattern isn't used
 * in this codebase's scripts.
 */
std::set<std::string> scanUsedAccessors(const slopengine::AssetStore& assets);

}
