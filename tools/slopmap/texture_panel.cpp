#include "texture_panel.hpp"

#include "material_browser.hpp"
#include "ui/icon_ui.hpp"

#include "map/uv_math.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slopmap {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

struct FaceTarget {
    int brush = -1;
    int face = -1;
};

std::vector<FaceTarget> collectTargets(const EditorDocument& doc) {
    std::vector<FaceTarget> targets;
    if (doc.selectionMode == SelectionMode::Face) {
        for (const FaceRef& ref : doc.selectedFaces) {
            if (!ref.valid() || ref.brush < 0 || ref.brush >= static_cast<int>(doc.brushes.size())) {
                continue;
            }
            const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(ref.brush)];
            if (ref.face < 0 || ref.face >= static_cast<int>(brush.faces.size())) {
                continue;
            }
            targets.push_back({ref.brush, ref.face});
        }
        return targets;
    }

    if (doc.selectionMode != SelectionMode::Brush) {
        return targets;
    }
    for (int index : doc.selectedBrushes) {
        if (index < 0 || index >= static_cast<int>(doc.brushes.size())) {
            continue;
        }
        const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(index)];
        for (int face = 0; face < static_cast<int>(brush.faces.size()); ++face) {
            targets.push_back({index, face});
        }
    }
    return targets;
}

slopengine::BrushFace* faceAt(EditorDocument& doc, const FaceTarget& target) {
    if (target.brush < 0 || target.brush >= static_cast<int>(doc.brushes.size())) {
        return nullptr;
    }
    slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(target.brush)];
    if (target.face < 0 || target.face >= static_cast<int>(brush.faces.size())) {
        return nullptr;
    }
    return &brush.faces[static_cast<std::size_t>(target.face)];
}

const slopengine::BrushFace* faceAt(const EditorDocument& doc, const FaceTarget& target) {
    if (target.brush < 0 || target.brush >= static_cast<int>(doc.brushes.size())) {
        return nullptr;
    }
    const slopengine::Brush& brush = doc.brushes[static_cast<std::size_t>(target.brush)];
    if (target.face < 0 || target.face >= static_cast<int>(brush.faces.size())) {
        return nullptr;
    }
    return &brush.faces[static_cast<std::size_t>(target.face)];
}

template <typename T>
std::optional<T> commonValue(
    const EditorDocument& doc,
    const std::vector<FaceTarget>& targets,
    const std::function<T(const slopengine::BrushFace&)>& getter) {
    std::optional<T> common;
    for (const FaceTarget& target : targets) {
        const slopengine::BrushFace* face = faceAt(doc, target);
        if (face == nullptr) {
            continue;
        }
        const T value = getter(*face);
        if (!common.has_value()) {
            common = value;
        } else if (*common != value) {
            return std::nullopt;
        }
    }
    return common;
}

bool nearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

Vector3 normalize3(Vector3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-8f) {
        return {};
    }
    return {v.x / len, v.y / len, v.z / len};
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 rotateAroundAxis(Vector3 v, Vector3 axis, float radians) {
    const Vector3 n = normalize3(axis);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const Vector3 term1 = {v.x * c, v.y * c, v.z * c};
    const Vector3 term2 = cross3(n, v);
    const Vector3 term2s = {term2.x * s, term2.y * s, term2.z * s};
    const float nd = dot3(n, v) * (1.0f - c);
    const Vector3 term3 = {n.x * nd, n.y * nd, n.z * nd};
    return {term1.x + term2s.x + term3.x, term1.y + term2s.y + term3.y, term1.z + term2s.z + term3.z};
}

float faceRotationDegrees(const slopengine::BrushFace& face) {
    Vector3 axialU{};
    Vector3 axialV{};
    slopengine::axialUvAxes(face.normal, axialU, axialV);
    Vector3 uAxis{};
    Vector3 vAxis{};
    slopengine::faceUvAxes(face, uAxis, vAxis);
    (void)vAxis;
    const float cosA = dot3(uAxis, axialU);
    const float sinA = dot3(uAxis, axialV);
    return std::atan2(sinA, cosA) * kRadToDeg;
}

void setFaceRotationDegrees(slopengine::BrushFace& face, float degrees) {
    Vector3 axialU{};
    Vector3 axialV{};
    slopengine::axialUvAxes(face.normal, axialU, axialV);
    const float radians = degrees * kDegToRad;
    face.uvLock = true;
    face.uvUAxis = normalize3(rotateAroundAxis(axialU, face.normal, radians));
    face.uvVAxis = normalize3(rotateAroundAxis(axialV, face.normal, radians));
}

void setFaceUvLock(slopengine::BrushFace& face, bool locked) {
    face.uvLock = locked;
    if (locked) {
        if (face.uvUAxis.x == 0.0f && face.uvUAxis.y == 0.0f && face.uvUAxis.z == 0.0f) {
            face.uvUAxis = {};
            face.uvVAxis = {};
        }
        slopengine::ensureFaceUvAxes(face);
    }
}

slopengine::MaterialUvInfo resolveMaterialUv(
    slopengine::AssetStore& assets,
    std::string_view materialPath) {
    slopengine::MaterialUvInfo info{};
    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialPath);
    if (asset != nullptr) {
        info.pixelsPerMeter = asset->pixelsPerMeter;
        if (!asset->albedoTexture.empty()) {
            const Texture2D texture = assets.getTexture(asset->albedoTexture);
            if (texture.id != 0 && texture.width > 0 && texture.height > 0) {
                info.textureWidth = static_cast<float>(texture.width);
                info.textureHeight = static_cast<float>(texture.height);
            }
        }
    }
    return info;
}

void fitFaceTexture(slopengine::BrushFace& face, slopengine::AssetStore& assets) {
    if (face.vertices.size() < 3) {
        return;
    }
    Vector3 uAxis{};
    Vector3 vAxis{};
    slopengine::faceUvAxes(face, uAxis, vAxis);
    float minU = 0.0f;
    float maxU = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
        const Vector3& p = face.vertices[i];
        const float u = p.x * uAxis.x + p.y * uAxis.y + p.z * uAxis.z;
        const float v = p.x * vAxis.x + p.y * vAxis.y + p.z * vAxis.z;
        if (i == 0) {
            minU = maxU = u;
            minV = maxV = v;
        } else {
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
    }
    const float rangeU = std::max(maxU - minU, 1e-5f);
    const float rangeV = std::max(maxV - minV, 1e-5f);
    const slopengine::MaterialUvInfo uvInfo = resolveMaterialUv(assets, face.material);
    const float ppm = uvInfo.pixelsPerMeter > 0.0f ? uvInfo.pixelsPerMeter : 64.0f;
    const float texW = uvInfo.textureWidth > 0.0f ? uvInfo.textureWidth : 64.0f;
    const float texH = uvInfo.textureHeight > 0.0f ? uvInfo.textureHeight : 64.0f;
    const float scaleU = texW / (rangeU * ppm);
    const float scaleV = texH / (rangeV * ppm);
    const float scale = std::min(scaleU, scaleV);
    face.uvScale = {scale, scale};
    face.uvShiftPixels.x = -0.5f * (minU + maxU) * ppm * scale + 0.5f * texW;
    face.uvShiftPixels.y = -0.5f * (minV + maxV) * ppm * scale + 0.5f * texH;
}

void resetFaceUv(slopengine::BrushFace& face) {
    face.uvShiftPixels = {};
    face.uvScale = {1.0f, 1.0f};
    face.uvLock = false;
    face.uvUAxis = {};
    face.uvVAxis = {};
}

enum class AlignComponent {
    X,
    Y,
    XY,
};

float axisScale(float scale) {
    return scale > 1e-8f ? scale : 1.0f;
}

bool nearlyEqual3(Vector3 a, Vector3 b, float eps = 1e-4f) {
    return nearlyEqual(a.x, b.x, eps) && nearlyEqual(a.y, b.y, eps) && nearlyEqual(a.z, b.z, eps);
}

std::optional<Vector3> findSharedVertex(
    const slopengine::BrushFace& a,
    const slopengine::BrushFace& b) {
    for (const Vector3& va : a.vertices) {
        for (const Vector3& vb : b.vertices) {
            if (nearlyEqual3(va, vb)) {
                return va;
            }
        }
    }
    return std::nullopt;
}

Vector3 alignSamplePoint(const slopengine::BrushFace& reference, const slopengine::BrushFace& target) {
    if (const std::optional<Vector3> shared = findSharedVertex(reference, target)) {
        return *shared;
    }
    if (!reference.vertices.empty()) {
        return reference.vertices[0];
    }
    if (!target.vertices.empty()) {
        return target.vertices[0];
    }
    return {};
}

void alignFaceUvToReference(
    slopengine::BrushFace& target,
    const slopengine::BrushFace& reference,
    AlignComponent component,
    slopengine::AssetStore& assets) {
    if (reference.vertices.empty() && target.vertices.empty()) {
        return;
    }
    const Vector3 sample = alignSamplePoint(reference, target);

    Vector3 refU{};
    Vector3 refV{};
    slopengine::faceUvAxes(reference, refU, refV);
    const slopengine::MaterialUvInfo refUvInfo = resolveMaterialUv(assets, reference.material);
    const Vector2 refUv = slopengine::worldPlanarUv(
        sample,
        refU,
        refV,
        reference.uvShiftPixels,
        reference.uvScale,
        refUvInfo);

    Vector3 tgtU{};
    Vector3 tgtV{};
    slopengine::faceUvAxes(target, tgtU, tgtV);
    const slopengine::MaterialUvInfo tgtUvInfo = resolveMaterialUv(assets, target.material);
    const float ppm = tgtUvInfo.pixelsPerMeter > 0.0f ? tgtUvInfo.pixelsPerMeter : 64.0f;
    const float texW = tgtUvInfo.textureWidth > 0.0f ? tgtUvInfo.textureWidth : 64.0f;
    const float texH = tgtUvInfo.textureHeight > 0.0f ? tgtUvInfo.textureHeight : 64.0f;
    const float metersU = sample.x * tgtU.x + sample.y * tgtU.y + sample.z * tgtU.z;
    const float metersV = sample.x * tgtV.x + sample.y * tgtV.y + sample.z * tgtV.z;

    if (component == AlignComponent::X || component == AlignComponent::XY) {
        target.uvShiftPixels.x =
            refUv.x * texW - metersU * ppm * axisScale(target.uvScale.x);
    }
    if (component == AlignComponent::Y || component == AlignComponent::XY) {
        target.uvShiftPixels.y =
            refUv.y * texH - metersV * ppm * axisScale(target.uvScale.y);
    }
}

bool alignTargetsToActive(
    Editor& editor,
    slopengine::AssetStore& assets,
    AlignComponent component) {
    EditorDocument& doc = editor.doc();
    if (doc.selectionMode != SelectionMode::Face || !doc.activeFace.valid()) {
        return false;
    }
    const FaceTarget activeTarget{doc.activeFace.brush, doc.activeFace.face};
    const slopengine::BrushFace* reference = faceAt(doc, activeTarget);
    if (reference == nullptr) {
        return false;
    }

    editor.prepareEdit();
    int count = 0;
    for (const FaceRef& ref : doc.selectedFaces) {
        if (!ref.valid() || ref == doc.activeFace) {
            continue;
        }
        slopengine::BrushFace* face = faceAt(doc, FaceTarget{ref.brush, ref.face});
        if (face == nullptr) {
            continue;
        }
        alignFaceUvToReference(*face, *reference, component, assets);
        ++count;
    }
    if (count == 0) {
        editor.abortEdit();
        return false;
    }
    editor.markDirty();
    editor.markRadDirty();
    return true;
}

bool forEachTarget(
    Editor& editor,
    const std::vector<FaceTarget>& targets,
    const std::function<void(slopengine::BrushFace&)>& fn) {
    EditorDocument& doc = editor.doc();
    editor.prepareEdit();
    int count = 0;
    for (const FaceTarget& target : targets) {
        slopengine::BrushFace* face = faceAt(doc, target);
        if (face == nullptr) {
            continue;
        }
        fn(*face);
        ++count;
    }
    if (count == 0) {
        editor.abortEdit();
        return false;
    }
    editor.markDirty();
    editor.markRadDirty();
    return true;
}

bool dragFloatMixed(const char* label, float* value, bool mixed, float speed) {
    if (mixed) {
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "—");
        const bool changed = ImGui::DragFloat(label, value, speed, 0.0f, 0.0f, overlay);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
        return changed;
    }
    return ImGui::DragFloat(label, value, speed);
}

bool checkboxMixed(const char* label, bool* value, bool mixed) {
    if (mixed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    const bool changed = ImGui::Checkbox(label, value);
    if (mixed) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }
    return changed;
}

void drawMaterialPreviewBox(float size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(ImGuiCol_FrameBg));
    draw->AddRect(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(ImGuiCol_Border));
}

void drawMaterialPreview(
    slopengine::AssetStore& assets,
    const std::string& materialLabel,
    float size) {
    const ImVec2 boxPos = ImGui::GetCursorScreenPos();
    drawMaterialPreviewBox(size);

    if (materialLabel == "mixed" || materialLabel == "none" || materialLabel == "(empty)") {
        return;
    }

    const slopengine::MaterialAsset* asset = assets.getMaterialAsset(materialLabel);
    if (asset == nullptr || asset->albedoTexture.empty()) {
        return;
    }
    Texture2D texture = assets.getTexture(asset->albedoTexture);
    if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    const float texW = static_cast<float>(texture.width);
    const float texH = static_cast<float>(texture.height);
    const float scale = std::min(size / texW, size / texH);
    const float drawW = texW * scale;
    const float drawH = texH * scale;
    const ImVec2 min{
        boxPos.x + (size - drawW) * 0.5f,
        boxPos.y + (size - drawH) * 0.5f,
    };
    const ImVec2 max{min.x + drawW, min.y + drawH};

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    ImGui::GetWindowDrawList()->AddImage(
        ImTextureID(static_cast<intptr_t>(texture.id)),
        min,
        max,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32_WHITE);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
}

} // namespace

TexturePanelResult TexturePanel::drawSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    float bodyHeight) {
    if (!ImGui::BeginChild("##texturesection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return {};
    }
    const TexturePanelResult result = drawUvSection(editor, assets, bodyHeight, false);
    ImGui::EndChild();
    return result;
}

TexturePanelResult TexturePanel::drawUvSection(
    Editor& editor,
    slopengine::AssetStore& assets,
    float bodyHeight,
    bool skipMaterialHeader) {
    TexturePanelResult result{};
    const bool ownChild = !skipMaterialHeader;
    if (ownChild &&
        !ImGui::BeginChild("##texturesection", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return result;
    }

    const EditorDocument& doc = editor.doc();
    const std::vector<FaceTarget> targets = collectTargets(doc);
    if (targets.empty()) {
        ImGui::TextDisabled("Select faces or brushes to edit UVs");
        if (ownChild) {
            ImGui::EndChild();
        }
        return result;
    }

    if (!skipMaterialHeader) {
        constexpr float kMaterialPreviewSize = 128.0f;
        const std::string materialLabel = selectionMaterialLabel(doc);
        drawMaterialPreview(assets, materialLabel, kMaterialPreviewSize);
        ImGui::Text("Material: %s", materialLabel.c_str());
        ImGui::Text("%d face(s)", static_cast<int>(targets.size()));
        ImGui::Separator();
    } else {
        ImGui::Text("%d face(s)", static_cast<int>(targets.size()));
    }

    const auto nodrawCommon = commonValue<bool>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.nodraw;
    });
    const auto lockCommon = commonValue<bool>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.uvLock;
    });
    const auto shiftXCommon = commonValue<float>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.uvShiftPixels.x;
    });
    const auto shiftYCommon = commonValue<float>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.uvShiftPixels.y;
    });
    const auto scaleXCommon = commonValue<float>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.uvScale.x;
    });
    const auto scaleYCommon = commonValue<float>(doc, targets, [](const slopengine::BrushFace& f) {
        return f.uvScale.y;
    });

    std::optional<float> rotationCommon;
    {
        bool have = false;
        float first = 0.0f;
        bool mixed = false;
        for (const FaceTarget& target : targets) {
            const slopengine::BrushFace* face = faceAt(doc, target);
            if (face == nullptr) {
                continue;
            }
            const float degrees = faceRotationDegrees(*face);
            if (!have) {
                first = degrees;
                have = true;
            } else if (!nearlyEqual(first, degrees, 0.05f)) {
                mixed = true;
                break;
            }
        }
        if (have && !mixed) {
            rotationCommon = first;
        }
    }

    bool nodraw = nodrawCommon.value_or(false);
    if (checkboxMixed("Nodraw", &nodraw, !nodrawCommon.has_value())) {
        if (forEachTarget(editor, targets, [nodraw](slopengine::BrushFace& face) {
                face.nodraw = nodraw;
            })) {
            result.changed = true;
            editor.statusMessage = nodraw ? "Nodraw on" : "Nodraw off";
        }
    }

    bool uvLock = lockCommon.value_or(false);
    if (checkboxMixed("UV Lock", &uvLock, !lockCommon.has_value())) {
        if (forEachTarget(editor, targets, [uvLock](slopengine::BrushFace& face) {
                setFaceUvLock(face, uvLock);
            })) {
            result.changed = true;
            editor.statusMessage = uvLock ? "UV lock on" : "UV lock off";
        }
    }

    float shiftX = shiftXCommon.value_or(0.0f);
    float shiftY = shiftYCommon.value_or(0.0f);
    if (dragFloatMixed("Shift X", &shiftX, !shiftXCommon.has_value(), 1.0f)) {
        if (forEachTarget(editor, targets, [shiftX](slopengine::BrushFace& face) {
                face.uvShiftPixels.x = shiftX;
            })) {
            result.changed = true;
        }
    }
    if (dragFloatMixed("Shift Y", &shiftY, !shiftYCommon.has_value(), 1.0f)) {
        if (forEachTarget(editor, targets, [shiftY](slopengine::BrushFace& face) {
                face.uvShiftPixels.y = shiftY;
            })) {
            result.changed = true;
        }
    }

    ImGui::Checkbox("Lock aspect", &lockAspect);
    float scaleX = scaleXCommon.value_or(1.0f);
    float scaleY = scaleYCommon.value_or(1.0f);
    if (dragFloatMixed("Scale X", &scaleX, !scaleXCommon.has_value(), 0.01f)) {
        if (scaleX < 1e-4f) {
            scaleX = 1e-4f;
        }
        if (lockAspect) {
            const float ratio =
                (scaleYCommon.has_value() && scaleXCommon.has_value() && scaleXCommon.value() > 1e-8f)
                ? (scaleYCommon.value() / scaleXCommon.value())
                : 1.0f;
            scaleY = scaleX * ratio;
        }
        if (forEachTarget(editor, targets, [&](slopengine::BrushFace& face) {
                face.uvScale.x = scaleX;
                if (lockAspect) {
                    face.uvScale.y = scaleY;
                }
            })) {
            result.changed = true;
        }
    }
    if (dragFloatMixed("Scale Y", &scaleY, !scaleYCommon.has_value(), 0.01f)) {
        if (scaleY < 1e-4f) {
            scaleY = 1e-4f;
        }
        if (lockAspect) {
            const float ratio =
                (scaleYCommon.has_value() && scaleXCommon.has_value() && scaleYCommon.value() > 1e-8f)
                ? (scaleXCommon.value() / scaleYCommon.value())
                : 1.0f;
            scaleX = scaleY * ratio;
        }
        if (forEachTarget(editor, targets, [&](slopengine::BrushFace& face) {
                face.uvScale.y = scaleY;
                if (lockAspect) {
                    face.uvScale.x = scaleX;
                }
            })) {
            result.changed = true;
        }
    }

    float rotation = rotationCommon.value_or(0.0f);
    if (dragFloatMixed("Rotation", &rotation, !rotationCommon.has_value(), 1.0f)) {
        if (forEachTarget(editor, targets, [rotation](slopengine::BrushFace& face) {
                setFaceRotationDegrees(face, rotation);
            })) {
            result.changed = true;
            editor.statusMessage = "Set UV rotation";
        }
    }

    ImGui::Separator();
    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "shape_handles", "Fit")) {
        if (forEachTarget(editor, targets, [&assets](slopengine::BrushFace& face) {
                fitFaceTexture(face, assets);
            })) {
            result.changed = true;
            editor.statusMessage = "Fit texture to face(s)";
        }
    }
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "arrow_undo", "Reset UV")) {
        if (forEachTarget(editor, targets, [](slopengine::BrushFace& face) {
                resetFaceUv(face);
            })) {
            result.changed = true;
            editor.statusMessage = "Reset face UVs";
        }
    }

    const bool canAlign = doc.selectionMode == SelectionMode::Face && doc.activeFace.valid() &&
        doc.selectedFaces.size() >= 2;
    auto alignTooltip = [canAlign]() {
        if (!canAlign && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select multiple faces; active face is the align source");
        }
    };
    if (!canAlign) {
        ImGui::BeginDisabled();
    }
    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "shape_align_left", "Align X")) {
        if (alignTargetsToActive(editor, assets, AlignComponent::X)) {
            result.changed = true;
            editor.statusMessage = "Aligned X to active face";
        }
    }
    alignTooltip();
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(assets, slopengine::kDefaultIconSet, "shape_align_top", "Align Y")) {
        if (alignTargetsToActive(editor, assets, AlignComponent::Y)) {
            result.changed = true;
            editor.statusMessage = "Aligned Y to active face";
        }
    }
    alignTooltip();
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(
            assets, slopengine::kDefaultIconSet, "shape_align_center", "Align X&Y")) {
        if (alignTargetsToActive(editor, assets, AlignComponent::XY)) {
            result.changed = true;
            editor.statusMessage = "Aligned X&Y to active face";
        }
    }
    alignTooltip();
    if (!canAlign) {
        ImGui::EndDisabled();
    }

    if (ownChild) {
        ImGui::EndChild();
    }
    return result;
}

}
