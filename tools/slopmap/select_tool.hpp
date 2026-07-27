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
    bool rotating = false;
    TranslateAxis axisLock = TranslateAxis::None;
    TranslateAxis rotateAxisLock = TranslateAxis::Y;
    Vector3 translateOrigin{};
    Vector3 rotateOrigin{};
    Vector3 mouseGrabWorld{};
    Vector2 mouseGrabScreen{};
    float rotateGrabAngle = 0.0f;
    float rotateAngle = 0.0f;
    std::vector<slopengine::Brush> brushSnapshot;
    std::vector<int> brushSnapshotIndices;
    std::vector<Vector3> entityAtSnapshots;
    std::vector<float> entityYawSnapshots;
    std::vector<Vector3> entityAnglesSnapshots;
    std::vector<bool> entityHaveAnglesSnapshots;
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
    void cancelRotate(Editor& editor);
    void toggleSelectedUvLock(Editor& editor);
    bool active() const { return translating || rotating; }
    bool numericLocked(const Editor& editor) const;

private:
    Vector2 pickCycleMouse{};
    std::vector<int> pickCycleBrushes;
    std::vector<FaceRef> pickCycleFaces;
    std::vector<EntityRef> pickCycleEntities;
    int pickCycleIndex = 0;

    void beginTranslate(Editor& editor, const Camera3D& camera);
    void applyTranslate(Editor& editor, slopengine::AssetStore& assets, Vector3 delta);
    void confirmTranslate(Editor& editor, slopengine::AssetStore& assets);
    void beginRotate(Editor& editor, const Camera3D& camera);
    void applyRotate(Editor& editor, slopengine::AssetStore& assets, float angleRadians);
    void confirmRotate(Editor& editor, slopengine::AssetStore& assets);
    void handleNumeric(Editor& editor, slopengine::AssetStore& assets, bool uiWantsKeyboard);
    void pick(Editor& editor, const Camera3D& camera);
    void deleteSelected(Editor& editor, slopengine::AssetStore& assets);
    void duplicateSelected(Editor& editor, const Camera3D& camera);
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
