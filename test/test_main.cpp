#include "test_assert.hpp"

#include <iostream>
#include <string_view>

namespace slopengine {

void runSexprTests();
void runAssetTests();
void runScriptScopeTests();
void runSchemeHardenTests();
void runBspBuildTests();
void runBspAnalyzeTests();
void runFacBuildTests();
void runPvsBuildTests();
void runPhysicsTests();
void runFrustumTests();
void runTransformTests();
void runDynamicLightTests();
void runBrushDoorTests();
void runBrushSplitTests();
void runSightTests();

}

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

const Suite kSuites[] = {
    {"sexpr", slopengine::runSexprTests},
    {"assets", slopengine::runAssetTests},
    {"script_scope", slopengine::runScriptScopeTests},
    {"scheme_harden", slopengine::runSchemeHardenTests},
    {"bsp_build", slopengine::runBspBuildTests},
    {"bsp_analyze", slopengine::runBspAnalyzeTests},
    {"fac_build", slopengine::runFacBuildTests},
    {"pvs_build", slopengine::runPvsBuildTests},
    {"physics", slopengine::runPhysicsTests},
    {"frustum", slopengine::runFrustumTests},
    {"transform", slopengine::runTransformTests},
    {"dynamic_light", slopengine::runDynamicLightTests},
    {"brush_door", slopengine::runBrushDoorTests},
    {"brush_split", slopengine::runBrushSplitTests},
    {"sight", slopengine::runSightTests},
};

bool runSuite(const Suite& suite) {
    const int before = sloptest::failureCount();
    std::cout << "[ RUN      ] " << suite.name << '\n';
    suite.run();
    const int failures = sloptest::failureCount() - before;
    if (failures == 0) {
        std::cout << "[       OK ] " << suite.name << '\n';
        return true;
    }
    std::cout << "[  FAILED  ] " << suite.name << " (" << failures << " check(s))\n";
    return false;
}

void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [suite...]\nSuites:";
    for (const Suite& suite : kSuites) {
        std::cerr << ' ' << suite.name;
    }
    std::cerr << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        bool ok = true;
        for (const Suite& suite : kSuites) {
            ok = runSuite(suite) && ok;
        }
        const int failures = sloptest::failureCount();
        if (failures == 0) {
            std::cout << "All tests passed.\n";
            return 0;
        }
        std::cerr << failures << " test failure(s).\n";
        return 1;
    }

    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view name = argv[i];
        if (name == "-h" || name == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        const Suite* found = nullptr;
        for (const Suite& suite : kSuites) {
            if (name == suite.name) {
                found = &suite;
                break;
            }
        }
        if (found == nullptr) {
            std::cerr << "Unknown suite: " << argv[i] << '\n';
            printUsage(argv[0]);
            return 2;
        }
        ok = runSuite(*found) && ok;
    }

    const int failures = sloptest::failureCount();
    if (failures == 0 && ok) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test failure(s).\n";
    return 1;
}
