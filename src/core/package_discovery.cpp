#include "core/package_discovery.hpp"

#include <system_error>
#include <unordered_set>

namespace slopengine {

std::vector<Package> discoverPackages(const std::vector<std::filesystem::path>& searchPaths) {
    std::vector<Package> packages;
    std::unordered_set<std::string> seenIds;

    for (const std::filesystem::path& root : searchPaths) {
        std::error_code ec;
        std::filesystem::directory_iterator it(root, ec);
        if (ec) {
            continue;
        }
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code isDirEc;
            if (!entry.is_directory(isDirEc) || isDirEc) {
                continue;
            }
            Package package{entry.path()};
            if (!package.hasMeta()) {
                continue;
            }
            if (!seenIds.insert(package.meta().id).second) {
                continue;
            }
            packages.push_back(std::move(package));
        }
    }

    return packages;
}

}
