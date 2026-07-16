#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

struct PackageMeta {
    std::string id;
    std::string name;
    std::string version;
    std::vector<std::string> depends;
};

bool parsePackageMeta(std::string_view source, PackageMeta& out);
std::optional<PackageMeta> loadPackageMetaFile(const std::filesystem::path& path);

}
