#pragma once

#include "editor.hpp"

#include "assets/asset_store.hpp"

#include <raylib.h>

namespace slopthing {

struct OrbitCamera {
    Vector3 target{0.0f, 0.6f, 0.0f};
    float distance = 3.0f;
    float yaw = 0.6f;
    float pitch = 0.3f;
    float lookSensitivity = 0.005f;
    float zoomSensitivity = 0.15f;

    Camera3D toRaylib() const;
    void update(bool allowInput);
    Vector3 position() const;
};

/** True when the selected thing has a sprite plus a motor or trigger-size
 * collider block — enough to make a size-comparison preview worthwhile. */
bool thingHasColliderPreview(const NodePtr& alist);

/** Resizes @p target to @p width x @p height, recreating it only if the size changed. */
bool ensureRenderTexture(RenderTexture2D& target, int width, int height);

struct ColliderPreview {
    OrbitCamera camera;

    void draw(Editor& editor, slopengine::AssetStore& assets, RenderTexture2D& target, bool allowInput);
};

}
