#include "world_preview.hpp"

#include "render/sprite_billboard.hpp"

#include <algorithm>
#include <optional>
#include <raymath.h>
#include <rlgl.h>

namespace slopsprite {

void WorldPreview::draw(Editor& editor, RenderTexture2D& target, bool allowInput) {
    if (editor.requestWorldCameraFrame) {
        framePending = true;
        editor.requestWorldCameraFrame = false;
    }

    camera.update(allowInput);

    BeginTextureMode(target);
    ClearBackground(Color{32, 36, 42, 255});

    const Camera3D rayCam = camera.toRaylib();
    BeginMode3D(rayCam);
    DrawGrid(20, 1.0f);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, RED);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, GREEN);
    DrawLine3D({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, BLUE);

    if (editor.doc.open && !editor.doc.atlasDirty) {
        slopengine::GlobalTransformation global{};
        global.matrix = MatrixScale(editor.doc.worldScale, editor.doc.worldScale, editor.doc.worldScale);
        const auto billboard = slopengine::resolveSpriteBillboard(
            editor.doc.asset,
            editor.doc.atlas,
            editor.doc.currentFrame,
            editor.doc.facingYaw,
            global,
            rayCam.position);
        if (billboard && billboard->texture != nullptr) {
            if (framePending) {
                Vector3 center{
                    (billboard->points[0].x + billboard->points[1].x + billboard->points[2].x +
                     billboard->points[3].x) *
                        0.25f,
                    (billboard->points[0].y + billboard->points[1].y + billboard->points[2].y +
                     billboard->points[3].y) *
                        0.25f,
                    (billboard->points[0].z + billboard->points[1].z + billboard->points[2].z +
                     billboard->points[3].z) *
                        0.25f,
                };
                float radius = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    const Vector3 delta = Vector3Subtract(billboard->points[i], center);
                    radius = std::max(radius, Vector3Length(delta));
                }
                if (radius < 0.05f) {
                    radius = 0.05f;
                }
                camera.frameBounds(center, radius);
                framePending = false;
            }

            const Texture2D& texture = *billboard->texture;
            const Rectangle source = billboard->source;
            const float texW = static_cast<float>(texture.width);
            const float texH = static_cast<float>(texture.height);
            const Vector2 texcoords[4] = {
                {source.x / texW, (source.y + source.height) / texH},
                {(source.x + source.width) / texW, (source.y + source.height) / texH},
                {(source.x + source.width) / texW, source.y / texH},
                {source.x / texW, source.y / texH},
            };
            rlSetTexture(texture.id);
            rlBegin(RL_QUADS);
            for (int i = 0; i < 4; ++i) {
                rlColor4ub(255, 255, 255, 255);
                rlTexCoord2f(texcoords[i].x, texcoords[i].y);
                rlVertex3f(
                    billboard->points[i].x, billboard->points[i].y, billboard->points[i].z);
            }
            rlEnd();
            rlSetTexture(0);
        }
    }

    EndMode3D();
    DrawText(
        "World  RMB orbit  Wheel zoom  WASD pan",
        8,
        8,
        16,
        Color{200, 200, 200, 220});
    EndTextureMode();
}

}
