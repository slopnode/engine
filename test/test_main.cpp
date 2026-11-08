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
void runRiggedAssetTests();
void runTextureGraphTests();
void runScriptScopeTests();
void runSchemeHardenTests();
void runProcRoleTests();
void runSimClockTests();
void runNetTests();
void runTransportTests();
void runEnetTransportTests();
void runSecureTransportTests();
void runNetProtocolTests();
void runNetSessionTests();
void runNetPredictionTests();
void runNetInterestTests();
void runNetHardeningTests();
void runNetPacingTests();
void runBotScriptTests();
void runMotorRulesTests();
void runNetSpawnTests();
void runNetPredictedEventTests();
void runNetAudienceTests();
void runNetHostStandInTests();
void runNetRewindTests();
void runHeadlessAssetTests();
void runNetGuardTests();
void runS7ReadTests();
void runBspBuildTests();
void runBspAnalyzeTests();
void runMapScriptTests();
void runCameraPathTests();
void runCsgCompileTests();
void runPvsBuildTests();
void runMapAreasTests();
void runMapMetaTests();
void runBspPolygonTests();
void runNavGraphTests();
void runNavMacroLinksTests();
void runNavBakeTests();
void runNavNavmeshBuildTests();
void runNavIoTests();
void runNavProfileRegistryTests();
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
void runLightProbeCubeTests();
void runAttachmentPathTests();
void runSeamStitchTests();
void runSightTests();
void runViewRecoilTests();
void runModelHitboxTests();
void runSkinCutTests();
void runRagdollTests();
void runSlopUiThemeTests();
void runTracerTests();

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
    {"rigged_assets", slopengine::runRiggedAssetTests},
    {"texture_graph", slopengine::runTextureGraphTests},
    {"script_scope", slopengine::runScriptScopeTests},
    {"scheme_harden", slopengine::runSchemeHardenTests},
    {"proc_role", slopengine::runProcRoleTests},
    {"sim_clock", slopengine::runSimClockTests},
    {"net", slopengine::runNetTests},
    {"transport", slopengine::runTransportTests},
    {"enet_transport", slopengine::runEnetTransportTests},
    {"secure_transport", slopengine::runSecureTransportTests},
    {"net_protocol", slopengine::runNetProtocolTests},
    {"net_session", slopengine::runNetSessionTests},
    {"net_prediction", slopengine::runNetPredictionTests},
    {"net_interest", slopengine::runNetInterestTests},
    {"net_hardening", slopengine::runNetHardeningTests},
    {"net_pacing", slopengine::runNetPacingTests},
    {"bot_script", slopengine::runBotScriptTests},
    {"motor_rules", slopengine::runMotorRulesTests},
    {"net_spawn", slopengine::runNetSpawnTests},
    {"net_predicted_events", slopengine::runNetPredictedEventTests},
    {"net_audience", slopengine::runNetAudienceTests},
    {"net_host_standins", slopengine::runNetHostStandInTests},
    {"net_rewind", slopengine::runNetRewindTests},
    {"headless_assets", slopengine::runHeadlessAssetTests},
    {"net_guards", slopengine::runNetGuardTests},
    {"s7_read", slopengine::runS7ReadTests},
    {"bsp_build", slopengine::runBspBuildTests},
    {"bsp_analyze", slopengine::runBspAnalyzeTests},
    {"map_script", slopengine::runMapScriptTests},
    {"camera_path", slopengine::runCameraPathTests},
    {"csg_compile", slopengine::runCsgCompileTests},
    {"pvs_build", slopengine::runPvsBuildTests},
    {"map_areas", slopengine::runMapAreasTests},
    {"map_meta", slopengine::runMapMetaTests},
    {"bsp_polygon", slopengine::runBspPolygonTests},
    {"nav_graph", slopengine::runNavGraphTests},
    {"nav_macro_links", slopengine::runNavMacroLinksTests},
    {"nav_bake", slopengine::runNavBakeTests},
    {"nav_navmesh_build", slopengine::runNavNavmeshBuildTests},
    {"nav_io", slopengine::runNavIoTests},
    {"nav_profile_registry", slopengine::runNavProfileRegistryTests},
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
    {"light_probe_cube", slopengine::runLightProbeCubeTests},
    {"attachment_path", slopengine::runAttachmentPathTests},
    {"seam_stitch", slopengine::runSeamStitchTests},
    {"sight", slopengine::runSightTests},
    {"view_recoil", slopengine::runViewRecoilTests},
    {"model_hitboxes", slopengine::runModelHitboxTests},
    {"skin_cut", slopengine::runSkinCutTests},
    {"ragdoll", slopengine::runRagdollTests},
    {"slopui_theme", slopengine::runSlopUiThemeTests},
    {"tracer", slopengine::runTracerTests},
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
