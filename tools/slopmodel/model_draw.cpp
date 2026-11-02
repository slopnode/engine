#include "model_draw.hpp"

#include "render/bone_attachment.hpp"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace slopmodel {

void ShadedDraw::init(slopengine::AssetStore& assets) {
    const std::string vert = assets.getShaderSource("default/viewmodel_vert");
    const std::string frag = assets.getShaderSource("default/viewmodel_frag");
    if (vert.empty() || frag.empty()) {
        TraceLog(LOG_WARNING, "slopmodel: missing viewmodel shaders, previews draw unlit");
        return;
    }
    shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (shader.id == 0) {
        return;
    }
    shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");

    const Vector3 probeRgb{1.0f, 1.0f, 1.0f};
    const Vector3 keyDir = Vector3Normalize({0.4f, 0.85f, 0.35f});
    const float ambient = 0.4f;
    const float keyStrength = 0.75f;
    const float rimStrength = 0.15f;
    SetShaderValue(shader, GetShaderLocation(shader, "probeRgb"), &probeRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "ambient"), &ambient, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "keyDir"), &keyDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "keyStrength"), &keyStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "rimStrength"), &rimStrength, SHADER_UNIFORM_FLOAT);
}

void ShadedDraw::release() {
    if (shader.id != 0) {
        UnloadShader(shader);
        shader = {};
    }
}

bool ShadedDraw::valid() const {
    return shader.id != 0;
}

void drawGeoModel(Model& model, const Matrix& transform, const ShadedDraw* shaded, Color tint, bool twoSided) {
    if (model.meshCount <= 0) {
        return;
    }
    std::vector<Shader> previous;
    const bool swap = shaded != nullptr && shaded->valid();
    if (swap) {
        previous.resize(static_cast<std::size_t>(model.materialCount));
        for (int i = 0; i < model.materialCount; ++i) {
            previous[static_cast<std::size_t>(i)] = model.materials[i].shader;
            model.materials[i].shader = shaded->shader;
        }
    }
    if (twoSided) {
        rlDisableBackfaceCulling();
    }
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(transform).v);
    DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, tint);
    rlPopMatrix();
    if (twoSided) {
        rlEnableBackfaceCulling();
    }
    if (swap) {
        for (int i = 0; i < model.materialCount; ++i) {
            model.materials[i].shader = previous[static_cast<std::size_t>(i)];
        }
    }
}

void drawSkeleton(const Model& model, int highlightBone) {
    if (model.skeleton.boneCount <= 0 || model.skeleton.bones == nullptr) {
        return;
    }
    const Transform* poses = model.currentPose != nullptr ? model.currentPose : model.skeleton.bindPose;
    if (poses == nullptr) {
        return;
    }
    std::vector<float> lengths;
    for (int bone = 0; bone < model.skeleton.boneCount; ++bone) {
        const int parent = model.skeleton.bones[bone].parent;
        if (parent >= 0 && parent < model.skeleton.boneCount) {
            const float length = Vector3Distance(poses[parent].translation, poses[bone].translation);
            if (length > 1e-5f) {
                lengths.push_back(length);
            }
        }
    }
    float jointRadius = 0.012f;
    if (!lengths.empty()) {
        std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
        jointRadius = std::clamp(lengths[lengths.size() / 2] * 0.15f, 0.002f, 0.02f);
    }
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    for (int bone = 0; bone < model.skeleton.boneCount; ++bone) {
        const Vector3 joint = poses[bone].translation;
        const bool highlighted = bone == highlightBone;
        DrawSphereEx(
            joint,
            highlighted ? jointRadius * 2.0f : jointRadius,
            4,
            6,
            highlighted ? Color{255, 90, 60, 255} : Color{255, 200, 40, 255});
        const int parent = model.skeleton.bones[bone].parent;
        if (parent >= 0 && parent < model.skeleton.boneCount) {
            DrawLine3D(poses[parent].translation, joint, Color{240, 180, 40, 220});
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
}

void drawEditorScene(Editor& editor, const Matrix& modelMatrix, const ShadedDraw* shaded, SceneDraw& out) {
    out.markers.clear();
    if (!editor.doc.open) {
        return;
    }
    if (editor.showMesh) {
        drawGeoModel(editor.doc.model, modelMatrix, editor.shaded ? shaded : nullptr, WHITE, editor.twoSided);
    }

    const int count = static_cast<int>(editor.doc.geo.attachments.size());
    for (int i = 0; i < count; ++i) {
        const Transform pose = editor.attachmentPose(i);
        const Matrix local = transformMatrix(pose);
        const Matrix world = MatrixMultiply(local, modelMatrix);
        AttachmentPreview& preview = editor.doc.previews[static_cast<std::size_t>(i)];
        if (preview.model.meshCount > 0) {
            drawGeoModel(preview.model, world, editor.shaded ? shaded : nullptr, WHITE, editor.twoSided);
        }
        if (!editor.showAttachments) {
            continue;
        }
        const Vector3 origin = Vector3Transform({0.0f, 0.0f, 0.0f}, world);
        const float axis = 0.08f;
        const Vector3 ex = Vector3Transform({axis, 0.0f, 0.0f}, world);
        const Vector3 ey = Vector3Transform({0.0f, axis, 0.0f}, world);
        const Vector3 ez = Vector3Transform({0.0f, 0.0f, axis}, world);
        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        DrawLine3D(origin, ex, RED);
        DrawLine3D(origin, ey, GREEN);
        DrawLine3D(origin, ez, BLUE);
        const bool selected = i == editor.doc.selectedAttachment;
        DrawSphereEx(origin, selected ? 0.02f : 0.012f, 4, 6, selected ? Color{255, 255, 120, 255} : Color{200, 200, 255, 255});
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        if (out.camera != nullptr) {
            SceneDraw::Marker marker{};
            marker.screen = GetWorldToScreenEx(
                origin,
                *out.camera,
                static_cast<int>(out.screenSize.x),
                static_cast<int>(out.screenSize.y));
            marker.name = editor.doc.geo.attachments[static_cast<std::size_t>(i)].name;
            marker.selected = selected;
            out.markers.push_back(std::move(marker));
        }
    }

    if (editor.showSkeleton && editor.skinned()) {
        int highlight = editor.doc.selectedBone;
        if (highlight < 0 && editor.doc.selectedAttachment >= 0 && editor.doc.selectedAttachment < count) {
            highlight = slopengine::findModelBoneIndex(
                editor.doc.model,
                editor.doc.geo.attachments[static_cast<std::size_t>(editor.doc.selectedAttachment)].bone);
        }
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloatV(modelMatrix).v);
        drawSkeleton(editor.doc.model, highlight);
        rlPopMatrix();
        const Model& model = editor.doc.model;
        const Transform* poses = model.currentPose != nullptr ? model.currentPose : model.skeleton.bindPose;
        if (out.camera != nullptr && poses != nullptr && editor.doc.selectedBone >= 0 &&
            editor.doc.selectedBone < model.skeleton.boneCount) {
            SceneDraw::Marker marker{};
            marker.screen = GetWorldToScreenEx(
                Vector3Transform(poses[editor.doc.selectedBone].translation, modelMatrix),
                *out.camera,
                static_cast<int>(out.screenSize.x),
                static_cast<int>(out.screenSize.y));
            marker.name = model.skeleton.bones[editor.doc.selectedBone].name;
            marker.selected = true;
            out.markers.push_back(std::move(marker));
        }
    }
}

void drawSceneLabels(const SceneDraw& scene) {
    for (const SceneDraw::Marker& marker : scene.markers) {
        const Color color = marker.selected ? Color{255, 255, 140, 255} : Color{220, 220, 255, 255};
        DrawText(
            marker.name.c_str(),
            static_cast<int>(marker.screen.x) + 8,
            static_cast<int>(marker.screen.y) - 6,
            12,
            color);
    }
}

}
