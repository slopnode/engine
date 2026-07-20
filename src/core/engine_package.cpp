#include "core/engine_package.hpp"

#include "core/engine_package_dir.hpp"

#include <cstdlib>

namespace slopengine {

namespace {

bool isEnginePackageRoot(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(root / "package.meta", ec);
}

std::optional<std::filesystem::path> tryPath(const std::filesystem::path& root) {
    if (root.empty() || !isEnginePackageRoot(root)) {
        return std::nullopt;
    }
    return root;
}

}

std::optional<std::filesystem::path> resolveEnginePackage() {
    if (auto found = tryPath(std::filesystem::current_path() / "packages" / "engine")) {
        return found;
    }

    if (const char* env = std::getenv("SLOPENGINE_ENGINE"); env != nullptr && env[0] != '\0') {
        if (auto found = tryPath(env)) {
            return found;
        }
    }

    if (auto found = tryPath(SLOPENGINE_ENGINE_PACKAGE_DIR)) {
        return found;
    }

    return std::nullopt;
}

}
