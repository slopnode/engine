#pragma once

#include <filesystem>

namespace slopengine {

/** A content package rooted at a directory on disk. */
class Package {
public:
    /** Constructs a package with the given root directory. */
    explicit Package(std::filesystem::path root);

    /** Returns the package root directory. */
    const std::filesystem::path& root() const { return root_; }

    /** Returns true when the root path exists and is a directory. */
    bool valid() const;

private:
    std::filesystem::path root_;
};

}
