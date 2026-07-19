#include "editor.hpp"

#include "core/vfs.hpp"
#include "map/csg_script.hpp"
#include "map/csg_write.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>

namespace slopmap {

namespace {

bool ensureMapFiles(
    const std::filesystem::path& baseGame,
    const std::string& mapName,
    const std::string& packageId,
    std::filesystem::path& outCsgPath) {
    if (mapName.empty()) {
        return false;
    }
    const std::filesystem::path mapDir = baseGame / "maps" / mapName;
    std::error_code ec;
    std::filesystem::create_directories(mapDir, ec);
    if (ec) {
        return false;
    }

    outCsgPath = mapDir / "static.csg";
    const std::filesystem::path metaPath = mapDir / "map.meta";
    if (!std::filesystem::exists(metaPath)) {
        std::ofstream meta(metaPath, std::ios::binary | std::ios::trunc);
        if (!meta) {
            return false;
        }
        meta << "(map\n";
        meta << "  (id \"" << mapName << "\")\n";
        meta << "  (name \"" << mapName << "\")\n";
        meta << "  (package \"" << packageId << "\")\n";
        meta << "  (depends \"" << packageId << "\")\n";
        meta << "  (ambient 0.03 0.03 0.04))\n";
    }
    return true;
}

void resetSelectionSerial(EditorDocument& doc) {
    doc.selectedBrush = -1;
    doc.selectedFace = -1;
    doc.scope = SelectionScope::Brush;
    doc.nextBrushSerial = 1;
    for (const slopengine::Brush& brush : doc.brushes) {
        if (brush.id.rfind("brush-", 0) == 0) {
            try {
                const int serial = std::stoi(brush.id.substr(6));
                doc.nextBrushSerial = std::max(doc.nextBrushSerial, serial + 1);
            } catch (...) {
            }
        }
    }
}

} // namespace

float snapToGrid(float value, float grid) {
    if (grid <= 0.0f) {
        return value;
    }
    return std::round(value / grid) * grid;
}

Vector3 snapToGrid(Vector3 value, float grid) {
    return {
        snapToGrid(value.x, grid),
        snapToGrid(value.y, grid),
        snapToGrid(value.z, grid),
    };
}

bool rayPlaneIntersection(Ray ray, Vector3 planePoint, Vector3 planeNormal, Vector3& outHit) {
    const float denom =
        ray.direction.x * planeNormal.x + ray.direction.y * planeNormal.y + ray.direction.z * planeNormal.z;
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    const Vector3 toPlane{
        planePoint.x - ray.position.x,
        planePoint.y - ray.position.y,
        planePoint.z - ray.position.z,
    };
    const float t =
        (toPlane.x * planeNormal.x + toPlane.y * planeNormal.y + toPlane.z * planeNormal.z) / denom;
    if (t < 0.0f) {
        return false;
    }
    outHit = {
        ray.position.x + ray.direction.x * t,
        ray.position.y + ray.direction.y * t,
        ray.position.z + ray.direction.z * t,
    };
    return true;
}

Ray mouseRay(const Camera3D& camera, Rectangle viewport) {
    const Vector2 mouse = GetMousePosition();
    const Vector2 local{
        mouse.x - viewport.x,
        mouse.y - viewport.y,
    };
    return GetScreenToWorldRayEx(
        local,
        camera,
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height));
}

Vector2 worldToViewportScreen(Vector3 world, const Camera3D& camera, Rectangle viewport) {
    const Vector2 local = GetWorldToScreenEx(
        world,
        camera,
        static_cast<int>(viewport.width),
        static_cast<int>(viewport.height));
    return {local.x + viewport.x, local.y + viewport.y};
}

ConstructionPlane constructionPlaneForView(ViewPlane view) {
    ConstructionPlane plane{};
    switch (view) {
    case ViewPlane::Front:
        plane.origin = {0.0f, 0.0f, 0.0f};
        plane.normal = {0.0f, 0.0f, 1.0f};
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = {0.0f, 1.0f, 0.0f};
        break;
    case ViewPlane::Side:
        plane.origin = {0.0f, 0.0f, 0.0f};
        plane.normal = {1.0f, 0.0f, 0.0f};
        plane.axisU = {0.0f, 0.0f, 1.0f};
        plane.axisV = {0.0f, 1.0f, 0.0f};
        break;
    case ViewPlane::Top:
    case ViewPlane::PerspectiveY0:
    default:
        plane.origin = {0.0f, 0.0f, 0.0f};
        plane.normal = {0.0f, 1.0f, 0.0f};
        plane.axisU = {1.0f, 0.0f, 0.0f};
        plane.axisV = {0.0f, 0.0f, 1.0f};
        break;
    }
    return plane;
}

void Editor::newMap(const std::string& mapName) {
    doc.mapName = mapName.empty() ? "untitled" : mapName;
    doc.brushes.clear();
    doc.dirty = false;
    resetSelectionSerial(doc);
    preview.clear();
    camera.position = {0.0f, 2.5f, 8.0f};
    camera.yaw = 3.14159265f;
    camera.pitch = -0.35f;
    camera.orthographic = false;
    viewPlane = ViewPlane::PerspectiveY0;
    statusMessage = "New map '" + doc.mapName + "'";
}

bool Editor::load(slopengine::AssetStore& assets, s7_scheme* scheme, const std::string& mapName) {
    auto brushes = slopengine::loadMapBrushes(scheme, assets, mapName);
    if (!brushes) {
        statusMessage = "Load failed: " + mapName;
        return false;
    }

    doc.mapName = mapName;
    doc.brushes = std::move(*brushes);
    doc.dirty = false;
    resetSelectionSerial(doc);
    rebuildPreview(assets);
    frameSelection();
    statusMessage = "Loaded " + mapName + " (" + std::to_string(doc.brushes.size()) + " brushes)";
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    if (doc.mapName.empty() || doc.mapName == "untitled") {
        showSaveAsModal = true;
        modalMapName = doc.mapName == "untitled" ? "" : doc.mapName;
        return false;
    }
    return saveAs(assets, doc.mapName);
}

bool Editor::saveAs(slopengine::AssetStore& assets, const std::string& mapName) {
    if (mapName.empty()) {
        statusMessage = "Save failed: empty map name";
        return false;
    }

    std::filesystem::path csgPath;
    auto existing = assets.resolvePath(slopengine::AssetKind::MapCsg, mapName + "/static");
    if (existing) {
        csgPath = *existing;
    } else {
        if (!ensureMapFiles(baseGamePath, mapName, packageId, csgPath)) {
            statusMessage = "Save failed: could not create map folder";
            return false;
        }
    }

    if (!slopengine::writeMapBrushes(csgPath, doc.brushes)) {
        statusMessage = "Save failed: write error";
        return false;
    }

    doc.mapName = mapName;
    doc.dirty = false;
    statusMessage = "Saved " + csgPath.string();
    return true;
}

void Editor::markDirty() {
    doc.dirty = true;
}

void Editor::rebuildPreview(slopengine::AssetStore& assets) {
    preview.rebuild(assets, doc.brushes);
}

void Editor::cycleGrid(int direction) {
    static constexpr float kSizes[] = {1.0f, 0.5f, 0.25f, 0.125f};
    int index = 2;
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(gridSize - kSizes[i]) < 1e-6f) {
            index = i;
            break;
        }
    }
    index = (index + direction + 4) % 4;
    gridSize = kSizes[index];
}

void Editor::setViewPlane(ViewPlane plane) {
    viewPlane = plane;
    switch (plane) {
    case ViewPlane::Top:
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = -1.45f;
        break;
    case ViewPlane::Front:
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = 0.0f;
        break;
    case ViewPlane::Side:
        camera.orthographic = true;
        camera.yaw = 1.5708f;
        camera.pitch = 0.0f;
        break;
    case ViewPlane::PerspectiveY0:
    default:
        camera.orthographic = false;
        break;
    }
}

void Editor::toggleOrthoTop() {
    if (viewPlane == ViewPlane::Top) {
        setViewPlane(ViewPlane::PerspectiveY0);
    } else {
        setViewPlane(ViewPlane::Top);
    }
}

std::string Editor::allocateBrushId() {
    return "brush-" + std::to_string(doc.nextBrushSerial++);
}

void Editor::frameSelection() {
    const Vector3 center = selectionCenter();
    camera.position = {center.x, center.y + 2.5f, center.z + 8.0f};
    camera.lookAt(center);
}

Vector3 Editor::selectionCenter() const {
    if (doc.selectedBrush >= 0 && doc.selectedBrush < static_cast<int>(doc.brushes.size())) {
        const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(doc.selectedBrush)];
        return {
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
    }
    if (!doc.brushes.empty()) {
        Vector3 mins = doc.brushes[0].mins;
        Vector3 maxs = doc.brushes[0].maxs;
        for (const slopengine::Brush& brush : doc.brushes) {
            mins.x = std::min(mins.x, brush.mins.x);
            mins.y = std::min(mins.y, brush.mins.y);
            mins.z = std::min(mins.z, brush.mins.z);
            maxs.x = std::max(maxs.x, brush.maxs.x);
            maxs.y = std::max(maxs.y, brush.maxs.y);
            maxs.z = std::max(maxs.z, brush.maxs.z);
        }
        return {
            0.5f * (mins.x + maxs.x),
            0.5f * (mins.y + maxs.y),
            0.5f * (mins.z + maxs.z),
        };
    }
    return {0.0f, 1.0f, 0.0f};
}

}
