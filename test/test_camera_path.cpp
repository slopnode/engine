#include "test_assert.hpp"

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/camera_path.hpp"
#include "map/graph_script.hpp"
#include "map/graph_write.hpp"

#include <raymath.h>
#include <s7.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

namespace slopengine {

namespace {

bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

bool nearVec(Vector3 a, Vector3 b, float eps = 1e-3f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

CameraPath makeSquarePath() {
    CameraPath path{};
    path.id = "square";
    path.interp = PathInterp::CatmullRom;
    path.duration = 1.0f;
    const Vector3 corners[] = {
        {0.0f, 1.0f, 0.0f},
        {4.0f, 1.0f, 0.0f},
        {4.0f, 1.0f, 4.0f},
        {0.0f, 1.0f, 4.0f},
    };
    float yaw = 0.0f;
    for (const Vector3& corner : corners) {
        PathKnot knot{};
        knot.at = corner;
        knot.yaw = yaw;
        yaw += 1.0f;
        path.knots.push_back(knot);
    }
    return path;
}

void runSamplerTests() {
    CameraPath path = makeSquarePath();
    CHECK_EQ(cameraPathSegmentCount(path), 3);
    CHECK(near(cameraPathDuration(path), 3.0f));

    for (std::size_t i = 0; i < path.knots.size(); ++i) {
        const CameraPose pose = sampleCameraPath(path, static_cast<float>(i));
        CHECK(nearVec(pose.position, path.knots[i].at));
        CHECK(near(pose.yaw, path.knots[i].yaw));
    }

    path.interp = PathInterp::Linear;
    const CameraPose mid = sampleCameraPath(path, 0.5f);
    CHECK(nearVec(mid.position, {2.0f, 1.0f, 0.0f}));
    CHECK(near(mid.yaw, 0.5f));

    path.knots[0].duration = 3.0f;
    CHECK(near(cameraPathDuration(path), 5.0f));
    CHECK(nearVec(sampleCameraPath(path, 1.5f).position, {2.0f, 1.0f, 0.0f}));
    CHECK(nearVec(sampleCameraPath(path, 3.5f).position, {4.0f, 1.0f, 2.0f}));
    path.knots[0].duration = 0.0f;

    path.ease = PathEase::Smooth;
    const CameraPose eased = sampleCameraPath(path, 0.25f);
    CHECK(eased.position.x < 1.0f);
    CHECK(near(sampleCameraPath(path, 0.5f).position.x, 2.0f));
    path.ease = PathEase::Linear;

    path.closed = true;
    CHECK_EQ(cameraPathSegmentCount(path), 4);
    CHECK(nearVec(sampleCameraPath(path, 4.0f).position, path.knots[0].at));
    path.closed = false;

    path.interp = PathInterp::CatmullRom;
    path.constantSpeed = true;
    path.knots[1].duration = 0.0f;
    float prevDist = 0.0f;
    Vector3 prev = sampleCameraPath(path, 0.0f).position;
    bool monotonic = true;
    float minStep = 1e9f;
    float maxStep = 0.0f;
    for (int i = 1; i <= 30; ++i) {
        const Vector3 next = sampleCameraPath(path, 3.0f * static_cast<float>(i) / 30.0f).position;
        const float step = Vector3Distance(prev, next);
        minStep = std::min(minStep, step);
        maxStep = std::max(maxStep, step);
        if (step < 0.0f) {
            monotonic = false;
        }
        prev = next;
        prevDist += step;
    }
    CHECK(monotonic);
    CHECK(maxStep - minStep < 0.35f);
    CHECK(near(prevDist, cameraPathLength(path), 0.25f));
    path.constantSpeed = false;

    CameraPath wrap{};
    wrap.interp = PathInterp::Linear;
    PathKnot a{};
    a.yaw = 3.0f;
    PathKnot b{};
    b.at = {1.0f, 0.0f, 0.0f};
    b.yaw = -3.0f;
    wrap.knots = {a, b};
    const CameraPose wrapped = sampleCameraPath(wrap, wrap.duration);
    CHECK(std::fabs(std::fabs(wrapped.yaw) - 3.0f) < 1e-3f);
    const float midYaw = sampleCameraPath(wrap, 0.5f * wrap.duration).yaw;
    CHECK(std::fabs(std::fabs(midYaw) - 3.14159f) < 1e-2f);

    CameraPath look{};
    look.interp = PathInterp::Linear;
    look.orient = PathOrient::LookAt;
    look.lookAt = {0.0f, 0.0f, 0.0f};
    PathKnot l0{};
    l0.at = {0.0f, 0.0f, -5.0f};
    PathKnot l1{};
    l1.at = {5.0f, 0.0f, 0.0f};
    look.knots = {l0, l1};
    const CameraPose lookPose = sampleCameraPath(look, 0.0f);
    CHECK(nearVec(cameraPoseForward(lookPose), {0.0f, 0.0f, 1.0f}));
    const CameraPose lookEnd = sampleCameraPath(look, look.duration);
    CHECK(nearVec(cameraPoseForward(lookEnd), {-1.0f, 0.0f, 0.0f}));

    look.orient = PathOrient::Tangent;
    const CameraPose tangent = sampleCameraPath(look, 0.5f * look.duration);
    CHECK(nearVec(cameraPoseForward(tangent), Vector3Normalize({1.0f, 0.0f, 1.0f})));

    path.fov = 60.0f;
    path.knots[2].fov = 90.0f;
    CHECK(near(sampleCameraPath(path, 0.0f).fov, 60.0f));
    CHECK(near(sampleCameraPath(path, 2.0f).fov, 90.0f));

    std::vector<Vector3> points;
    tessellateCameraPath(path, 8, points);
    CHECK_EQ(points.size(), 25u);
    CHECK(nearVec(points.front(), path.knots.front().at));
    CHECK(nearVec(points.back(), path.knots.back().at));

    CHECK(near(applyPathEase(PathEase::Smooth, 0.5f), 0.5f));
    CHECK(near(applyPathEase(PathEase::In, 0.5f), 0.25f));
    CHECK(near(applyPathEase(PathEase::Out, 0.5f), 0.75f));
}

std::filesystem::path makeTempPackageRoot() {
    std::random_device rd;
    const auto suffix = std::to_string(rd());
    const auto root = std::filesystem::temp_directory_path() / ("sloptest-camera-path-" + suffix);
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

void runLoadWriteTests() {
    const std::filesystem::path root = makeTempPackageRoot();
    writeFile(
        root / "package.meta",
        "(package\n"
        "  (id \"sloptest-camera-path\")\n"
        "  (name \"Camera Path Test Package\")\n"
        "  (version \"0.1.0\")\n"
        "  (depends))\n");
    writeFile(
        root / "maps" / "testmap" / "map.meta",
        "(map (id \"testmap\") (name \"testmap\") (author \"\") (description \"\") (depends))\n");
    writeFile(
        root / "maps" / "testmap" / "graphs.s7",
        "(graph \"patrol\"\n"
        "  (node \"a\" (at 0 0 0) (tags \"start\"))\n"
        "  (node \"b\" (at 1 0 0))\n"
        "  (edge \"a\" \"b\" (cost 2) (bidir #t)))\n"
        "(path \"intro\"\n"
        "  (interp \"catmull-rom\")\n"
        "  (orient 'look-at)\n"
        "  (look-at 1 2 3)\n"
        "  (closed #t)\n"
        "  (constant-speed #t)\n"
        "  (duration 4)\n"
        "  (fov 70)\n"
        "  (ease \"smooth\")\n"
        "  (knot (at 0 1 0) (look 0.5 -0.25) (fov 80) (duration 2.5) (ease \"in\"))\n"
        "  (knot (at 4 1 0) (look 1 0))\n"
        "  (knot (at 4 1 4)))\n");

    AppConfig config{};
    config.game = root;
    AssetStore assets(config);
    s7_scheme* scheme = s7_init();
    CHECK_TRUE(scheme != nullptr);

    auto doc = loadMapGraphs(scheme, assets, "testmap");
    CHECK_TRUE(doc.has_value());
    if (doc.has_value()) {
        CHECK_EQ(doc->graphs.size(), 1u);
        CHECK_EQ(doc->paths.size(), 1u);
        const CameraPath* path = doc->findPath("intro");
        CHECK_TRUE(path != nullptr);
        if (path != nullptr) {
            CHECK_EQ(path->knots.size(), 3u);
            CHECK(path->interp == PathInterp::CatmullRom);
            CHECK(path->orient == PathOrient::LookAt);
            CHECK(nearVec(path->lookAt, {1.0f, 2.0f, 3.0f}));
            CHECK(path->closed);
            CHECK(path->constantSpeed);
            CHECK(near(path->duration, 4.0f));
            CHECK(near(path->fov, 70.0f));
            CHECK(path->ease == PathEase::Smooth);
            CHECK(nearVec(path->knots[0].at, {0.0f, 1.0f, 0.0f}));
            CHECK(near(path->knots[0].yaw, 0.5f));
            CHECK(near(path->knots[0].pitch, -0.25f));
            CHECK(near(path->knots[0].fov, 80.0f));
            CHECK(near(path->knots[0].duration, 2.5f));
            CHECK(path->knots[0].haveEase);
            CHECK(path->knots[0].ease == PathEase::In);
            CHECK(!path->knots[1].haveEase);
            CHECK(near(path->knots[2].yaw, 0.0f));
        }

        const std::string text = formatMapGraphs(*doc);
        writeFile(root / "maps" / "testmap" / "graphs.s7", text);
        auto again = loadMapGraphs(scheme, assets, "testmap");
        CHECK_TRUE(again.has_value());
        if (again.has_value()) {
            CHECK_EQ(formatMapGraphs(*again), text);
            CHECK_EQ(again->graphs.size(), 1u);
            if (!again->graphs.empty()) {
                CHECK_EQ(again->graphs[0].edges.size(), 1u);
                if (!again->graphs[0].edges.empty()) {
                    CHECK(near(again->graphs[0].edges[0].cost, 2.0f));
                    CHECK(again->graphs[0].edges[0].bidir);
                }
                CHECK_EQ(again->graphs[0].nodes.size(), 2u);
                if (!again->graphs[0].nodes.empty()) {
                    CHECK_EQ(again->graphs[0].nodes[0].tags.size(), 1u);
                }
            }
        }
    }

    s7_quit(scheme);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

} // namespace

void runCameraPathTests() {
    runSamplerTests();
    runLoadWriteTests();
}

}
