#pragma once

#include <raylib.h>
#include <raymath.h>

#include <string>

namespace slopengine {

struct LocalTransformation {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 scale = {1.0f, 1.0f, 1.0f};
    Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct GlobalTransformation {
    Matrix matrix = MatrixIdentity();
};

struct WorldSpace {};

struct Lens {
    Camera3D camera = {
        .position = {0.0f, 10.0f, 10.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE,
    };
};

struct Model3D {
    Model model = {};
    Color color = WHITE;
};

struct ShaderCavity {
    Shader shader = {};
};

struct Spin {
    Vector3 axis = {0.0f, 1.0f, 0.0f};
    float speed = 1.0f;
};

struct AnimationClipFlipTest {
    std::string clipA = "bob";
    std::string clipB = "default";
};

struct SpriteInstance {
    std::string sprite;
    std::string frame = "A";
    float facingYaw = 0.0f;
};

}
