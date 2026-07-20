#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** Fields from a package.meta file. */
struct PackageMeta {
    std::string id;       /**< Unique package id (required). */
    std::string name;     /**< Display name. */
    std::string version;  /**< Version string. */
    std::vector<std::string> depends; /**< Package ids that must be mounted. */
};

/** Parses package.meta text into @p out. */
bool parsePackageMeta(std::string_view source, PackageMeta& out);

/** Loads and parses package.meta at @p path. */
std::optional<PackageMeta> loadPackageMetaFile(const std::filesystem::path& path);

}
