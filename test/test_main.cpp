#include "test_assert.hpp"

#include <raylib.h>

#include <iostream>
#include <string_view>
#include <vector>

namespace slopengine {

void runSexprTests();
void runPackageSearchTests();
void runPackageMetaTests();
void runAssetTests();
void runScriptScopeTests();
void runSchemeHardenTests();
void runBspBuildTests();
void runBspAnalyzeTests();
void runMapScriptTests();
void runCsgCompileTests();
void runPvsBuildTests();
void runNavGraphTests();
void runNavMacroLinksTests();
void runNavBakeTests();
void runNavNavmeshBuildTests();
void runNavIoTests();
void runPhysicsTests();
void runFrustumTests();
void runTransformTests();
void runDynamicLightTests();
void runDynamicLightCompositingTests();
void runBrushDoorTests();
void runBrushSplitTests();
void runBrushExtrudeTests();
void runBrushCarveTests();
void runBrushRoleTests();
void runBrushBlocksTests();
void runLightmapTransparentTests();
void runLightmapRgbeTests();
void runLightmapMergeTests();
void runLightmapPackTests();
void runRadiosityEmitterTests();
void runSunShadowSoftnessTests();
void runSeamStitchTests();
void runSightTests();

}

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

const Suite kSuites[] = {
    {"sexpr", slopengine::runSexprTests},
    {"package_search", slopengine::runPackageSearchTests},
    {"package_meta", slopengine::runPackageMetaTests},
    {"assets", slopengine::runAssetTests},
    {"script_scope", slopengine::runScriptScopeTests},
    {"scheme_harden", slopengine::runSchemeHardenTests},
    {"bsp_build", slopengine::runBspBuildTests},
    {"bsp_analyze", slopengine::runBspAnalyzeTests},
    {"map_script", slopengine::runMapScriptTests},
    {"csg_compile", slopengine::runCsgCompileTests},
    {"pvs_build", slopengine::runPvsBuildTests},
    {"nav_graph", slopengine::runNavGraphTests},
    {"nav_macro_links", slopengine::runNavMacroLinksTests},
    {"nav_bake", slopengine::runNavBakeTests},
    {"nav_navmesh_build", slopengine::runNavNavmeshBuildTests},
    {"nav_io", slopengine::runNavIoTests},
    {"physics", slopengine::runPhysicsTests},
    {"frustum", slopengine::runFrustumTests},
    {"transform", slopengine::runTransformTests},
    {"dynamic_light", slopengine::runDynamicLightTests},
    {"dynamic_light_compositing", slopengine::runDynamicLightCompositingTests},
    {"brush_door", slopengine::runBrushDoorTests},
    {"brush_split", slopengine::runBrushSplitTests},
    {"brush_extrude", slopengine::runBrushExtrudeTests},
    {"brush_carve", slopengine::runBrushCarveTests},
    {"brush_role", slopengine::runBrushRoleTests},
    {"brush_blocks", slopengine::runBrushBlocksTests},
    {"lightmap_transparent", slopengine::runLightmapTransparentTests},
    {"lightmap_rgbe", slopengine::runLightmapRgbeTests},
    {"lightmap_merge", slopengine::runLightmapMergeTests},
    {"lightmap_pack", slopengine::runLightmapPackTests},
    {"radiosity_emitters", slopengine::runRadiosityEmitterTests},
    {"sun_shadow_softness", slopengine::runSunShadowSoftnessTests},
    {"seam_stitch", slopengine::runSeamStitchTests},
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
    std::cerr << "Usage: " << argv0 << " [--verbose] [suite...]\nSuites:";
    for (const Suite& suite : kSuites) {
        std::cerr << ' ' << suite.name;
    }
    std::cerr << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> suiteNames;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view name = argv[i];
        if (name == "-h" || name == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (name == "-v" || name == "--verbose") {
            verbose = true;
            continue;
        }
        suiteNames.push_back(name);
    }
    SetTraceLogLevel(verbose ? LOG_INFO : LOG_NONE);

    if (suiteNames.empty()) {
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
    for (const std::string_view& name : suiteNames) {
        const Suite* found = nullptr;
        for (const Suite& suite : kSuites) {
            if (name == suite.name) {
                found = &suite;
                break;
            }
        }
        if (found == nullptr) {
            std::cerr << "Unknown suite: " << name << '\n';
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
