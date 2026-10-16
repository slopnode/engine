#include "material_panel.hpp"

#include "drag_drop.hpp"
#include "panel_common.hpp"

#include "ui/icon_ui.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace slopmat {

namespace {

constexpr float kLabelWidth = 110.0f;

void labeledField(const char* label) {
    slopmat::labeledField(label, kLabelWidth);
}

bool colorEdit(const char* id, Color& color) {
    float rgba[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    if (!ImGui::ColorEdit4(id, rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        return false;
    }
    auto channel = [](float f) { return static_cast<unsigned char>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    color = {channel(rgba[0]), channel(rgba[1]), channel(rgba[2]), channel(rgba[3])};
    return true;
}

bool pathField(
    Editor& editor,
    slopengine::AssetStore& assets,
    AssetPicker& picker,
    const char* id,
    const char* hint,
    std::string& value,
    const char* pickTitle,
    std::vector<std::string> choices,
    const char* icon,
    DragPayload payload,
    DocKind pickKind) {
    const float pickWidth = iconButtonWidth() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(-pickWidth);
    char buf[256] = {};
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::InputTextWithHint("##path", hint, buf, sizeof(buf))) {
        value = buf;
        changed = true;
    }
    if (acceptDroppedPath(payload, value)) {
        changed = true;
    }
    ImGui::SameLine();
    if (slopengine::iconButton(assets, kIcons, icon, ImVec2(0.0f, 0.0f))) {
        std::string* target = &value;
        picker.show(pickTitle, std::move(choices), icon, pickKind, [&editor, target](const std::string& path) {
            *target = path;
            editor.markDirty();
        });
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", pickTitle);
    }
    ImGui::PopID();
    return changed;
}

void flagField(Editor& editor, const char* label, const char* id, bool& value, const char* tooltip) {
    labeledField(label);
    if (ImGui::Checkbox(id, &value)) {
        editor.markDirty();
    }
    if (tooltip != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

} // namespace

void drawMaterialInspector(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser) {
    slopengine::MaterialAsset& m = editor.doc.material;

    ImGui::TextDisabled("%s", editor.doc.virtualPath.c_str());
    if (!editor.doc.problem.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", editor.doc.problem.c_str());
    }

    ImGui::SeparatorText("Surface");
    labeledField("Shader");
    char shaderBuf[128] = {};
    std::snprintf(shaderBuf, sizeof(shaderBuf), "%s", m.shader.c_str());
    if (ImGui::InputText("##shader", shaderBuf, sizeof(shaderBuf))) {
        m.shader = shaderBuf;
        editor.markDirty();
    }

    int source = !m.textureGraphPath.empty() ? 2 : (!m.textureAnimPath.empty() ? 1 : 0);
    labeledField("Albedo from");
    bool sourceChanged = false;
    sourceChanged |= ImGui::RadioButton("Texture", &source, 0);
    ImGui::SameLine();
    sourceChanged |= ImGui::RadioButton("Animation", &source, 1);
    ImGui::SameLine();
    sourceChanged |= ImGui::RadioButton("Graph", &source, 2);
    if (sourceChanged) {
        if (source != 0) {
            m.albedoTexture.clear();
        }
        if (source != 1) {
            m.textureAnimPath.clear();
        }
        if (source != 2) {
            m.textureGraphPath.clear();
        }
        editor.markDirty();
    }

    if (source == 0) {
        labeledField("Texture");
        if (pathField(editor, assets, picker, "albedo", "textures/<path>", m.albedoTexture, "Pick Texture", browser.pathsOfKind(DocKind::Texture), "picture", DragPayload::Texture, DocKind::Texture)) {
            editor.markDirty();
        }
    } else if (source == 1) {
        labeledField("Animation");
        if (pathField(editor, assets, picker, "anim", "textures/<path>.texanim", m.textureAnimPath, "Pick Texture Animation", browser.pathsOfKind(DocKind::TextureAnim), "film", DragPayload::TextureAnim, DocKind::TextureAnim)) {
            editor.markDirty();
        }
        if (!m.textureAnimPath.empty() && assets.hasTextureAnim(m.textureAnimPath)) {
            ImGui::SameLine(kLabelWidth);
            if (slopengine::buttonWithIcon(assets, kIcons, "film", "Open animation")) {
                const std::string path = m.textureAnimPath;
                editor.loadTextureAnim(assets, path);
                return;
            }
        }
    } else {
        labeledField("Graph");
        if (pathField(editor, assets, picker, "graph", "textures/<path>.texgraph", m.textureGraphPath, "Pick Texture Graph", browser.pathsOfKind(DocKind::TextureGraph), "layers", DragPayload::TextureGraph, DocKind::TextureGraph)) {
            editor.markDirty();
        }
        if (!m.textureGraphPath.empty() && assets.hasTextureGraph(m.textureGraphPath)) {
            ImGui::SameLine(kLabelWidth);
            if (slopengine::buttonWithIcon(assets, kIcons, "layers", "Open graph")) {
                const std::string path = m.textureGraphPath;
                editor.loadTextureGraph(assets, path);
                return;
            }
        }
    }

    labeledField("Texel size");
    if (ImGui::DragFloat("##texel", &m.pixelsPerMeter, 1.0f, 1.0f, 4096.0f, "%.0f px/m")) {
        editor.markDirty();
    }
    labeledField("Base color");
    if (colorEdit("##basecolor", m.baseColor)) {
        editor.markDirty();
    }
    labeledField("IOR");
    if (ImGui::DragFloat("##ior", &m.ior, 0.01f, 1.0f, 3.0f, "%.2f")) {
        editor.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bake-time index of refraction; 1.0 = no bend");
    }

    ImGui::SeparatorText("Emission");
    labeledField("Mask");
    if (pathField(editor, assets, picker, "emission", source == 2 ? "graph \"emission\" output when empty" : "textures/<path>", m.emissionTexture, "Pick Emission Mask", browser.pathsOfKind(DocKind::Texture), "picture", DragPayload::Texture, DocKind::Texture)) {
        editor.markDirty();
    }
    labeledField("Color");
    if (colorEdit("##emissioncolor", m.emissionColor)) {
        editor.markDirty();
    }
    labeledField("Power");
    if (ImGui::DragFloat("##power", &m.emissionPower, 0.1f, 0.0f, 100.0f, "%.2f")) {
        editor.markDirty();
    }
    labeledField("Range");
    if (ImGui::DragFloat("##range", &m.emissionRange, 0.5f, 0.0f, 1000.0f, "%.1f")) {
        editor.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Max cast distance in world units; 0 = unlimited");
    }
    flagField(editor, "Fullbright", "##fullbright", m.fullbright, "Render-time: masked texels ignore lighting");
    labeledField("Blend");
    int blendMode = m.emissionMultiply ? 1 : 0;
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##blendmode", &blendMode, "add\0multiply\0")) {
        m.emissionMultiply = blendMode == 1;
        editor.markDirty();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("##blendscale", &m.emissionBlendScale, 0.05f, 0.0f, 10.0f, "scale %.2f")) {
        editor.markDirty();
    }
    flagField(editor, "Bake emission", "##bake", m.bakeEmission, "Bake this face's own emission into its own lightmap");
    flagField(editor, "Precise", "##precise", m.preciseEmission, "Denser bake sampling so mask features cast distinct shapes");
    flagField(editor, "Exact", "##exact", m.exactEmission, nullptr);

    ImGui::SeparatorText("Sky");
    flagField(editor, "Sky surface", "##sky", m.sky, "Bake-time sun aperture; not a lightmap receiver");
    if (m.haveSkyMode) {
        const char* mode = "solid";
        switch (m.skyMode) {
        case slopengine::SkyboxMode::Solid: mode = "solid color"; break;
        case slopengine::SkyboxMode::Gradient: mode = "gradient"; break;
        case slopengine::SkyboxMode::Cube: mode = "cube"; break;
        case slopengine::SkyboxMode::Cylinder: mode = "cylinder"; break;
        }
        ImGui::TextDisabled("Appearance: %s (edit in the .mat for now; saving keeps it)", mode);
    }

    if (ImGui::CollapsingHeader("Acoustics")) {
        slopengine::MaterialSoundProfile& s = m.sound;
        labeledField("Absorption");
        float absorption[3] = {s.absorptionLow, s.absorptionMid, s.absorptionHigh};
        if (ImGui::DragFloat3("##absorption", absorption, 0.01f, 0.0f, 1.0f, "%.3f")) {
            s.absorptionLow = absorption[0];
            s.absorptionMid = absorption[1];
            s.absorptionHigh = absorption[2];
            editor.markDirty();
        }
        labeledField("Scattering");
        if (ImGui::DragFloat("##scattering", &s.scattering, 0.01f, 0.0f, 1.0f, "%.3f")) {
            editor.markDirty();
        }
        labeledField("Transmission");
        float transmission[3] = {s.transmissionLow, s.transmissionMid, s.transmissionHigh};
        if (ImGui::DragFloat3("##transmission", transmission, 0.01f, 0.0f, 1.0f, "%.3f")) {
            s.transmissionLow = transmission[0];
            s.transmissionMid = transmission[1];
            s.transmissionHigh = transmission[2];
            editor.markDirty();
        }
    }
}

}
