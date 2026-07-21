#pragma once

#include <raylib.h>
#include <raymath.h>

#include <string>

namespace slopengine {

/** Local pose relative to the parent entity (or world root). */
struct LocalTransformation {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 scale = {1.0f, 1.0f, 1.0f};
    Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

/** Cached world matrix after hierarchy update. */
struct GlobalTransformation {
    Matrix matrix = MatrixIdentity();
};

/** Tag: entity is drawn and lit in the world pass. */
struct WorldSpace {};

/** Tag: entity is drawn in the fixed eye-space first-person pass. */
struct ViewSpace {};

/** Package view canvas in virtual pixels (from data/view.s7 *view-canvas*). */
struct ViewCanvas {
    int width = 320;
    int height = 200;
};

/** Screen-space FP sprite: placed in view-canvas pixels (origin = frame bottom-center). */
struct ViewSprite {
    float canvasX = 160.0f;
    float canvasY = 200.0f;
};

/** Active view camera (usually on Player). */
struct Lens {
    Camera3D camera = {
        .position = {0.0f, 10.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
};

/** Drawable raylib model with a tint color. */
struct Model3D {
    Model model = {};
    Color color = WHITE;
};

/** Optional cavity / AO style shader binding on an entity. */
struct ShaderCavity {
    Shader shader = {};
};

/** Simple spin animator around @p axis. */
struct Spin {
    Vector3 axis = {0.0f, 1.0f, 0.0f};
    float speed = 1.0f;
};

/** Demo helper that flips between two animation clips. */
struct AnimationClipFlipTest {
    std::string clipA = "bob";
    std::string clipB = "default";
};

/** Billboard sprite presentation: asset path, frame id, and facing yaw. */
struct SpriteInstance {
    std::string sprite;
    std::string frame = "A";
    float facingYaw = 0.0f;
};

}
