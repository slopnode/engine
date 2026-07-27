#pragma once

#include "core/package.hpp"

#include <string>
#include <string_view>

namespace slopengine {

class PackageLoadContextGuard {
public:
    PackageLoadContextGuard(std::string_view packageId, PackageRole role);
    ~PackageLoadContextGuard();

    PackageLoadContextGuard(const PackageLoadContextGuard&) = delete;
    PackageLoadContextGuard& operator=(const PackageLoadContextGuard&) = delete;

private:
    std::string previousId_;
    PackageRole previousRole_;
};

std::string_view currentPackageLoadId();
PackageRole currentPackageRole();

}
