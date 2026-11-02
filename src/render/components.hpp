#pragma once

#include <raylib.h>
#include <raymath.h>

#include "assets/bone_scale.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace slopengine {

/** Local pose relative to the parent entity (or world root).
 *  @ingroup render_components
 */
struct LocalTransformation {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 scale = {1.0f, 1.0f, 1.0f};
    Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

/** Cached world matrix after hierarchy update.
 *  @ingroup render_components
 */
struct GlobalTransformation {
    Matrix matrix = MatrixIdentity();
};

/** Tag: entity is drawn and lit in the world pass.
 *  @ingroup render_components
 */
struct WorldSpace {};

/** Tag: entity is drawn in the fixed eye-space first-person pass.
 *  @ingroup render_components
 */
struct ViewSpace {};

struct ViewGeoRoot {};

/** Package view canvas in virtual pixels (from data/view.s7 *view-canvas*).
 *  @ingroup render_components
 */
struct ViewCanvas {
    int width = 320;
    int height = 200;
};

/** Package HUD canvas in virtual pixels (from data/view.s7 *hud-canvas*).
 *  @ingroup render_components
 */
struct HudCanvas {
    int width = 320;
    int height = 200;
};

/** Screen-space FP sprite: anchorX/Y place the normalized origin on the view canvas.
 *  offsetX/Y are a package presentation layer (raise/lower/bob) on top of anchor.
 *  @ingroup render_components
 */
struct ViewSprite {
    float anchorX = 160.0f;
    float anchorY = 200.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotationDeg = 0.0f;
    float originX = 0.5f;
    float originY = 1.0f;
};

/** Active view camera (usually on Player).
 *  @ingroup render_components
 */
struct Lens {
    Camera3D camera = {
        .position = {0.0f, 10.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
};

/** Secondary render-to-texture camera (e.g. a weapon scope), synced from the
 *  Player's Lens each frame; @p fovy controls the zoom independently of the
 *  main view.
 *  @ingroup render_components
 */
struct ExtraEye {
    Camera3D camera = {
        .position = {0.0f, 0.0f, 0.0f},
        .target = {0.0f, 0.0f, 1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 20.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
    RenderTexture2D target = {};
    int width = 128;
    int height = 128;
    float fovy = 20.0f;
    bool active = false;
};

/** Script-controlled override to hide the first-person weapon viewmodel
 *  (e.g. while looking through a scope).
 *  @ingroup render_components
 */
struct FpViewOverride {
    bool hideWeapon = false;
};

/** Drawable raylib model with a tint color.
 *  @ingroup render_components
 */
struct Model3D {
    Model model = {};
    Color color = WHITE;
    bool ownsGpu = false;
    BoundingBox meshBounds = {};
    const Mesh* meshBoundsSource = nullptr;
    int meshBoundsCount = 0;
    float boundsSpread = 1.0f;
    float boundsLift = 0.0f;
};

struct BoneScales {
    NamedBoneScales named;
    std::vector<BoneScale> resolved;
    const BoneInfo* resolvedFor = nullptr;
    bool dirty = true;
};

struct ShaderParams {
    std::vector<std::pair<std::string, Vector4>> values;
};

inline BoundingBox modelLocalBounds(Model3D& model3d) {
    const Model& model = model3d.model;
    if (model3d.meshBoundsSource != model.meshes || model3d.meshBoundsCount != model.meshCount) {
        Model untransformed = model;
        untransformed.transform = MatrixIdentity();
        model3d.meshBounds = GetModelBoundingBox(untransformed);
        model3d.meshBoundsSource = model.meshes;
        model3d.meshBoundsCount = model.meshCount;
    }
    BoundingBox bounds = model3d.meshBounds;
    if (model3d.boundsSpread != 1.0f || model3d.boundsLift != 0.0f) {
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
        const Vector3 half = Vector3Scale(Vector3Subtract(bounds.max, bounds.min), 0.5f * model3d.boundsSpread);
        bounds.min = Vector3Subtract(center, half);
        bounds.max = Vector3Add(center, half);
        bounds.min.y += std::min(model3d.boundsLift, 0.0f);
        bounds.max.y += std::max(model3d.boundsLift, 0.0f);
    }
    return {
        Vector3Transform(bounds.min, model.transform),
        Vector3Transform(bounds.max, model.transform),
    };
}

/** The geometry asset a Model3D was built from, so attachments and view defaults can be
 *  looked up by name later.
 *  @ingroup render_components
 */
struct GeoSource {
    std::string path;
};

/** Tag: mesh carries its own baked lightmap UV2 + atlas (e.g. a door frozen to its closed
 *  pose); skip the runtime flat-tint probe and sample the atlas instead.
 *  @ingroup render_components
 */
struct BakedLightmapModel {};

/** Optional cavity / AO style shader binding on an entity.
 *  @ingroup render_components
 */
struct ShaderCavity {
    Shader shader = {};
    int modelLoc = -1;
    int viewLoc = -1;
    int projectionLoc = -1;
    bool resolved = false;
};

/** Simple spin animator around @p axis.
 *  @ingroup render_components
 */
struct Spin {
    Vector3 axis = {0.0f, 1.0f, 0.0f};
    float speed = 1.0f;
};

/** Demo helper that flips between two animation clips.
 *  @ingroup render_components
 */
struct AnimationClipFlipTest {
    std::string clipA = "bob";
    std::string clipB = "default";
};

/** Billboard sprite presentation: asset path, frame id, and facing yaw.
 *  @ingroup render_components
 */
struct SpriteInstance {
    std::string sprite;
    std::string frame = "A";
    float facingYaw = 0.0f;
};

/** Layered sprite spawned from a .spanim (overlay ...) hold cue.
 *  @ingroup render_components
 */
struct SpriteOverlay {
    int layer = 1;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    std::uint64_t host = 0;
};

/** Bitmask of hitmask part indices (bit (partIndex-1)) hidden from rendering, e.g. a shot-off head.
 *  @ingroup render_components
 */
struct SpriteHiddenParts {
    std::uint32_t mask = 0;
};

}
