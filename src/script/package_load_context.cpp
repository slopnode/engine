#include "script/package_load_context.hpp"

namespace slopengine {

namespace {

thread_local std::string g_currentPackageId;
thread_local PackageRole g_currentPackageRole = PackageRole::Base;

} // namespace

PackageLoadContextGuard::PackageLoadContextGuard(std::string_view packageId, PackageRole role)
    : previousId_(g_currentPackageId)
    , previousRole_(g_currentPackageRole) {
    g_currentPackageId.assign(packageId.begin(), packageId.end());
    g_currentPackageRole = role;
}

PackageLoadContextGuard::~PackageLoadContextGuard() {
    g_currentPackageId = std::move(previousId_);
    g_currentPackageRole = previousRole_;
}

std::string_view currentPackageLoadId() {
    return g_currentPackageId;
}

PackageRole currentPackageRole() {
    return g_currentPackageRole;
}

}
