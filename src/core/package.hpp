#pragma once

#include "core/package_meta.hpp"

#include <filesystem>

namespace slopengine {

enum class PackageRole {
    Engine,
    Base,
    Mod,
};

/** A mounted content package rooted at a directory with package.meta. */
class Package {
public:
    /** Loads metadata from @p root / package.meta when present. */
    explicit Package(std::filesystem::path root);

    /** Package root directory on disk. */
    const std::filesystem::path& root() const { return root_; }

    /** Parsed package.meta fields. */
    const PackageMeta& meta() const { return meta_; }

    /** Mount role assigned when the package is mounted. */
    PackageRole role() const { return role_; }

    /** Sets the mount role (engine / base / mod). */
    void setRole(PackageRole role) { role_ = role; }

    /** True when the root directory exists. */
    bool valid() const;

    /** True when package.meta supplied a non-empty id. */
    bool hasMeta() const { return !meta_.id.empty(); }

private:
    std::filesystem::path root_;
    PackageMeta meta_{};
    PackageRole role_ = PackageRole::Mod;
};

}
