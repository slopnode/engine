#include "core/package_search.hpp"

#include "core/app_search_paths.hpp"

#include <string_view>
#include <system_error>

namespace slopengine {

namespace {

bool hasPackageMeta(const std::filesystem::path& root) {
    std::error_code ec;
    return !root.empty() && std::filesystem::is_regular_file(root / "package.meta", ec);
}

} // namespace

std::vector<std::filesystem::path> defaultApplicationSearchPaths() {
    std::vector<std::filesystem::path> paths;

    const std::string_view raw = kSlopengineAppSearchPathsRaw;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t newline = raw.find('\n', start);
        const std::string_view line = (newline == std::string_view::npos)
            ? raw.substr(start)
            : raw.substr(start, newline - start);
        if (!line.empty()) {
            paths.emplace_back(std::string(line));
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }

    return paths;
}

std::vector<std::filesystem::path> applicationSearchPaths(
    const std::vector<std::filesystem::path>& userPaths) {
    std::vector<std::filesystem::path> paths = userPaths;
    const std::vector<std::filesystem::path> defaults = defaultApplicationSearchPaths();
    paths.insert(paths.end(), defaults.begin(), defaults.end());
    return paths;
}

std::filesystem::path resolveApplicationPackagePath(
    const std::filesystem::path& nameOrPath,
    const std::vector<std::filesystem::path>& searchPaths) {
    if (hasPackageMeta(nameOrPath)) {
        return nameOrPath;
    }

    for (const std::filesystem::path& root : searchPaths) {
        const std::filesystem::path candidate = root / nameOrPath;
        if (hasPackageMeta(candidate)) {
            return candidate;
        }
    }

    return nameOrPath;
}

}
