#include "core/package_search.hpp"

#include "test_assert.hpp"

#include <fstream>
#include <random>

namespace slopengine {

namespace {

std::filesystem::path makeTempDir(std::string_view label) {
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path()
        / ("sloptest_" + std::string(label) + "_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writePackageMeta(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    std::ofstream out(root / "package.meta");
    out << "(id \"test\") (version \"1.0\")\n";
}

} // namespace

void runPackageSearchTests() {
    const std::filesystem::path root = makeTempDir("package_search");

    const std::filesystem::path searchRootA = root / "search_a";
    const std::filesystem::path searchRootB = root / "search_b";
    std::filesystem::create_directories(searchRootA);
    std::filesystem::create_directories(searchRootB);

    writePackageMeta(searchRootA / "mygame");
    writePackageMeta(searchRootB / "mygame");
    writePackageMeta(searchRootB / "onlyinb");

    const std::vector<std::filesystem::path> searchPaths{searchRootA, searchRootB};

    {
        const std::filesystem::path resolved = resolveApplicationPackagePath("mygame", searchPaths);
        CHECK_EQ(resolved, searchRootA / "mygame");
    }

    {
        const std::filesystem::path resolved = resolveApplicationPackagePath("onlyinb", searchPaths);
        CHECK_EQ(resolved, searchRootB / "onlyinb");
    }

    {
        const std::filesystem::path resolved = resolveApplicationPackagePath("nowhere", searchPaths);
        CHECK_EQ(resolved, std::filesystem::path("nowhere"));
    }

    {
        const std::filesystem::path explicitPath = searchRootB / "mygame";
        const std::filesystem::path resolved = resolveApplicationPackagePath(explicitPath, searchPaths);
        CHECK_EQ(resolved, explicitPath);
    }

    {
        const std::vector<std::filesystem::path> combined = applicationSearchPaths({searchRootB});
        CHECK_TRUE(!combined.empty());
        CHECK_EQ(combined.front(), searchRootB);
    }

    std::filesystem::remove_all(root);
}

}
