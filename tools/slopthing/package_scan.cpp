#include "package_scan.hpp"

#include "assets/asset_store.hpp"
#include "core/package.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace slopthing {

const std::vector<ConventionField>& conventionFields() {
    static const std::vector<ConventionField> fields = {
        {"thing-def-health", "", "health", ConventionField::Type::Int, "Health"},
        {"thing-def-idle-anim", "", "idle-anim", ConventionField::Type::String, "Idle anim"},
        {"thing-def-behavior", "", "behavior", ConventionField::Type::String, "Behavior"},
        {"thing-def-pain-chance", "", "pain-chance", ConventionField::Type::Float, "Pain chance"},
        {"thing-def-pain-threshold",
         "",
         "pain-threshold",
         ConventionField::Type::Float,
         "Pain threshold"},
    };
    return fields;
}

namespace {

void scanFile(
    const std::filesystem::path& path,
    const std::vector<std::string>& needles,
    std::set<std::string>& found) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    for (const std::string& needle : needles) {
        if (found.count(needle) != 0) {
            continue;
        }
        if (text.find(needle) != std::string::npos) {
            found.insert(needle);
        }
    }
}

}

std::set<std::string> scanUsedAccessors(const slopengine::AssetStore& assets) {
    std::set<std::string> found;

    std::vector<std::string> needles;
    needles.reserve(conventionFields().size());
    for (const ConventionField& field : conventionFields()) {
        needles.push_back(field.accessor);
    }

    for (const slopengine::Package& package : assets.packages()) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(package.root(), ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".s7") {
                continue;
            }
            scanFile(it->path(), needles, found);
            if (found.size() == needles.size()) {
                return found;
            }
        }
    }

    return found;
}

}
