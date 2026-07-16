#pragma once

#include "core/package_meta.hpp"

#include <filesystem>

namespace slopengine {

class Package {
public:
    explicit Package(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    const PackageMeta& meta() const { return meta_; }
    bool valid() const;
    bool hasMeta() const { return !meta_.id.empty(); }

private:
    std::filesystem::path root_;
    PackageMeta meta_{};
};

}
