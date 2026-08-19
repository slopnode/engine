#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

/** A package dependency: an id, with an optional semver constraint. */
struct PackageDependency {
    std::string id;                /**< Package id that must be mounted. */
    std::string versionConstraint; /**< e.g. ">=1.2.0"; empty means any version. */
};

/** Fields from a package.meta file. */
struct PackageMeta {
    std::string id;       /**< Unique package id (required). */
    std::string name;     /**< Display name. */
    std::string version;  /**< Version string. */
    std::vector<PackageDependency> depends; /**< Packages that must be mounted. */
};

/** Parses package.meta text into @p out. */
bool parsePackageMeta(std::string_view source, PackageMeta& out);

/** Loads and parses package.meta at @p path. */
std::optional<PackageMeta> loadPackageMetaFile(const std::filesystem::path& path);

}
