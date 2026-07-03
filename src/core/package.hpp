#pragma once

#include <filesystem>

namespace daggerlike {

class Package {
public:
    explicit Package(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    bool valid() const;

private:
    std::filesystem::path root_;
};

}
