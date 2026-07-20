#pragma once

#include "core/package_meta.hpp"

#include <filesystem>

namespace slopengine {

/** A mounted content package rooted at a directory with package.meta. */
class Package {
public:
    /** Loads metadata from @p root / package.meta when present. */
    explicit Package(std::filesystem::path root);

    /** Package root directory on disk. */
    const std::filesystem::path& root() const { return root_; }

    /** Parsed package.meta fields. */
    const PackageMeta& meta() const { return meta_; }

    /** True when the root directory exists. */
    bool valid() const;

    /** True when package.meta supplied a non-empty id. */
    bool hasMeta() const { return !meta_.id.empty(); }

private:
    std::filesystem::path root_;
    PackageMeta meta_{};
};

}
