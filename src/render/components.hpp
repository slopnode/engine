#pragma once

#include <raylib.h>
#include <raymath.h>

#include <cstdint>
#include <string>

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

/** Screen-space FP sprite: canvasX/Y place the normalized origin on the view canvas.
 *  offsetX/Y are a package presentation layer (raise/lower/bob) on top of canvas.
 *  @ingroup render_components
 */
struct ViewSprite {
    float canvasX = 160.0f;
    float canvasY = 200.0f;
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

/** Drawable raylib model with a tint color.
 *  @ingroup render_components
 */
struct Model3D {
    Model model = {};
    Color color = WHITE;
    bool ownsGpu = false;
};

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

}
