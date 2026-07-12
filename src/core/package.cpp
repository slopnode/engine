#include "core/package.hpp"

namespace slopengine {

Package::Package(std::filesystem::path root)
    : root_{std::move(root)} {
}

bool Package::valid() const {
    return !root_.empty() && std::filesystem::is_directory(root_);
}

}
