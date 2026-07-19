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
    bool numericActive = false;

    void update(Editor& editor, const Camera3D& camera, bool uiWantsMouse, bool uiWantsKeyboard);
    void cancelTranslate(Editor& editor);
    bool active() const { return translating; }

private:
    void beginTranslate(Editor& editor, const Camera3D& camera);
    void applyTranslate(Editor& editor, Vector3 delta);
    void confirmTranslate(Editor& editor);
    void handleNumeric(Editor& editor, bool uiWantsKeyboard);
    void pick(Editor& editor, const Camera3D& camera);
    void deleteSelected(Editor& editor);
    void duplicateSelected(Editor& editor, const Camera3D& camera);
};

std::optional<float> rayBrushHitDistance(Ray ray, const slopengine::Brush& brush);
std::optional<int> rayBrushFaceIndex(Ray ray, const slopengine::Brush& brush, float* outDistance = nullptr);

}
