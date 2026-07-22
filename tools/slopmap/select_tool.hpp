#pragma once

#include "editor.hpp"
#include "map/brush.hpp"

#include <raylib.h>

#include <optional>
#include <vector>

namespace slopmap {

enum class TranslateAxis {
    None,
    X,
    Y,
    Z,
};

struct SelectTool {
    bool translating = false;
    TranslateAxis axisLock = TranslateAxis::None;
    Vector3 translateOrigin{};
    Vector3 mouseGrabWorld{};
    std::vector<slopengine::Brush> brushSnapshot;
    std::vector<int> brushSnapshotIndices;
    std::vector<Vector3> entityAtSnapshots;
    std::vector<EntityRef> entitySnapshotRefs;
    FaceRef faceTranslate{};
    bool numericActive = false;

    void update(
        Editor& editor,
        slopengine::AssetStore& assets,
        const Camera3D& camera,
        bool uiWantsMouse,
        bool uiWantsKeyboard);
    void cancelTranslate(Editor& editor);
    void toggleSelectedUvLock(Editor& editor);
    bool active() const { return translating; }

private:
    Vector2 pickCycleMouse{};
    std::vector<int> pickCycleBrushes;
    std::vector<FaceRef> pickCycleFaces;
    std::vector<EntityRef> pickCycleEntities;
    int pickCycleIndex = 0;

    void beginTranslate(Editor& editor, const Camera3D& camera);
    void applyTranslate(Editor& editor, slopengine::AssetStore& assets, Vector3 delta);
    void confirmTranslate(Editor& editor, slopengine::AssetStore& assets);
    void handleNumeric(Editor& editor, slopengine::AssetStore& assets, bool uiWantsKeyboard);
    void pick(Editor& editor, const Camera3D& camera);
    void deleteSelected(Editor& editor, slopengine::AssetStore& assets);
    void duplicateSelected(Editor& editor, const Camera3D& camera);
    void rotateSelected(Editor& editor, slopengine::AssetStore& assets);
};

std::optional<float> rayBrushHitDistance(
    Ray ray,
    const slopengine::Brush& brush,
    bool ignoreBackfaces = false);
std::optional<int> rayBrushFaceIndex(
    Ray ray,
    const slopengine::Brush& brush,
    float* outDistance = nullptr,
    bool ignoreBackfaces = false);

}
