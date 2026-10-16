#include "graph_panel.hpp"

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

using slopengine::TextureGraph;
using slopengine::TextureGraphBlend;
using slopengine::TextureGraphLayer;
using slopengine::TextureGraphNode;
using slopengine::TextureGraphNodeKind;

constexpr float kLabelWidth = 90.0f;

constexpr TextureGraphNodeKind kKinds[] = {
    TextureGraphNodeKind::Image,
    TextureGraphNodeKind::Color,
    TextureGraphNodeKind::Checker,
    TextureGraphNodeKind::Gradient,
    TextureGraphNodeKind::Noise,
    TextureGraphNodeKind::Bricks,
    TextureGraphNodeKind::Stripes,
    TextureGraphNodeKind::GradientMap,
    TextureGraphNodeKind::Transform,
    TextureGraphNodeKind::Levels,
    TextureGraphNodeKind::Hsv,
    TextureGraphNodeKind::Invert,
    TextureGraphNodeKind::Blur,
    TextureGraphNodeKind::Sharpen,
    TextureGraphNodeKind::Edges,
    TextureGraphNodeKind::Stack,
};

constexpr slopengine::TextureGraphNoiseType kNoiseTypes[] = {
    slopengine::TextureGraphNoiseType::Value,
    slopengine::TextureGraphNoiseType::Perlin,
    slopengine::TextureGraphNoiseType::Worley,
};

constexpr TextureGraphBlend kBlends[] = {
    TextureGraphBlend::Normal,
    TextureGraphBlend::Multiply,
    TextureGraphBlend::Add,
    TextureGraphBlend::Screen,
};

void labeledField(const char* label) {
    slopmat::labeledField(label, kLabelWidth);
}

bool deleteButton(slopengine::AssetStore& assets, const char* tooltip) {
    return deleteIconButton(assets, tooltip);
}

bool isReferenced(const TextureGraph& graph, const std::string& id) {
    for (const TextureGraphNode& node : graph.nodes) {
        for (const std::string& ref : slopengine::textureGraphNodeReferences(node)) {
            if (ref == id) {
                return true;
            }
        }
    }
    for (const slopengine::TextureGraphOutput& output : graph.outputs) {
        if (output.source == id) {
            return true;
        }
    }
    return false;
}

void renameNode(TextureGraph& graph, const std::string& from, const std::string& to) {
    for (TextureGraphNode& node : graph.nodes) {
        if (node.input == from) {
            node.input = to;
        }
        for (TextureGraphLayer& layer : node.layers) {
            if (layer.source == from) {
                layer.source = to;
            }
            if (layer.mask == from) {
                layer.mask = to;
            }
        }
    }
    for (slopengine::TextureGraphOutput& output : graph.outputs) {
        if (output.source == from) {
            output.source = to;
        }
    }
}

std::string uniqueNodeId(const TextureGraph& graph, const char* stem) {
    for (int n = 1;; ++n) {
        const std::string candidate = std::string(stem) + "-" + std::to_string(n);
        if (graph.findNode(candidate) == nullptr) {
            return candidate;
        }
    }
}

std::string nextOutputName(const TextureGraph& graph) {
    if (graph.findOutput("albedo") == nullptr) {
        return "albedo";
    }
    if (graph.findOutput("emission") == nullptr) {
        return "emission";
    }
    for (int n = 1;; ++n) {
        const std::string candidate = "output-" + std::to_string(n);
        if (graph.findOutput(candidate) == nullptr) {
            return candidate;
        }
    }
}

bool nodeCombo(const char* label, const TextureGraph& graph, std::string& value, const std::string& excludeId, bool allowNone = false) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str())) {
        if (allowNone && ImGui::Selectable("(none)", value.empty())) {
            value.clear();
            changed = true;
        }
        for (const TextureGraphNode& node : graph.nodes) {
            if (node.id == excludeId) {
                continue;
            }
            if (ImGui::Selectable(node.id.c_str(), node.id == value)) {
                value = node.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool sizeField(Editor& editor, TextureGraphNode& node, const char* hint) {
    labeledField("Size");
    int size[2] = {node.width, node.height};
    bool changed = false;
    if (ImGui::InputInt2("##size", size)) {
        node.width = std::clamp(size[0], 0, 4096);
        node.height = std::clamp(size[1], 0, 4096);
        editor.markDirty();
        changed = true;
    }
    if (hint != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", hint);
    }
    return changed;
}

void colorField(Editor& editor, const char* label, const char* id, Vector4& color) {
    labeledField(label);
    float rgba[4] = {color.x, color.y, color.z, color.w};
    if (ImGui::ColorEdit4(id, rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        color = {rgba[0], rgba[1], rgba[2], rgba[3]};
        editor.markDirty();
    }
}

void drawImageFields(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser, TextureGraphNode& node) {
    labeledField("Texture");
    const float pickWidth = iconButtonWidth() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(-pickWidth);
    char buf[256] = {};
    std::snprintf(buf, sizeof(buf), "%s", node.texture.c_str());
    if (ImGui::InputTextWithHint("##texture", "virtual path under textures/", buf, sizeof(buf))) {
        node.texture = buf;
        editor.markDirty();
    }
    if (acceptDroppedPath(DragPayload::Texture, node.texture)) {
        editor.markDirty();
    }
    ImGui::SameLine();
    if (slopengine::iconButton(assets, kIcons, "picture", ImVec2(0.0f, 0.0f))) {
        TextureGraphNode* target = &node;
        picker.show("Pick Texture", browser.pathsOfKind(DocKind::Texture), "picture", DocKind::Texture, [&editor, target](const std::string& path) {
            target->texture = path;
            editor.markDirty();
        });
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pick a texture");
    }
}

void drawLayer(Editor& editor, slopengine::AssetStore& assets, TextureGraphNode& node, std::size_t index, bool& removed, int& moveDir) {
    TextureGraphLayer& layer = node.layers[index];
    ImGui::PushID(static_cast<int>(index));

    const float buttonW = iconButtonWidth();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float controls = (buttonW + spacing) * 3.0f;
    const float half = (ImGui::GetContentRegionAvail().x - controls - spacing) * 0.5f;

    ImGui::SetNextItemWidth(half);
    if (nodeCombo("##source", editor.doc.graph, layer.source, node.id)) {
        editor.markDirty();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    if (ImGui::BeginCombo("##blend", slopengine::textureGraphBlendName(layer.blend))) {
        for (const TextureGraphBlend blend : kBlends) {
            if (ImGui::Selectable(slopengine::textureGraphBlendName(blend), blend == layer.blend)) {
                layer.blend = blend;
                editor.markDirty();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (slopengine::iconButton(assets, kIcons, "arrow_up", ImVec2(0.0f, 0.0f))) {
        moveDir = -1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= node.layers.size());
    if (slopengine::iconButton(assets, kIcons, "arrow_down", ImVec2(0.0f, 0.0f))) {
        moveDir = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (deleteButton(assets, "Remove layer")) {
        removed = true;
    }

    labeledField("Opacity");
    if (ImGui::SliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f")) {
        editor.markDirty();
    }
    labeledField("Offset");
    int offset[2] = {layer.offsetX, layer.offsetY};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
    if (ImGui::InputInt2("##offset", offset)) {
        layer.offsetX = offset[0];
        layer.offsetY = offset[1];
        editor.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Repeat", &layer.repeat)) {
        editor.markDirty();
    }
    colorField(editor, "Tint", "##tint", layer.tint);
    labeledField("Mask");
    if (nodeCombo("##mask", editor.doc.graph, layer.mask, node.id, true)) {
        editor.markDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Node whose luminance scales this layer's alpha, sampled in canvas space");
    }

    ImGui::PopID();
}

void floatField(Editor& editor, const char* label, const char* id, float& value, float speed, float lo, float hi, const char* format) {
    labeledField(label);
    if (ImGui::DragFloat(id, &value, speed, lo, hi, format)) {
        editor.markDirty();
    }
}

void inputField(Editor& editor, TextureGraphNode& node) {
    labeledField("Input");
    if (nodeCombo("##input", editor.doc.graph, node.input, node.id)) {
        editor.markDirty();
    }
}

void drawStops(Editor& editor, slopengine::AssetStore& assets, TextureGraphNode& node) {
    ImGui::TextDisabled("Stops (luminance 0..1 to color)");
    bool removedAny = false;
    std::size_t removeIndex = 0;
    for (std::size_t i = 0; i < node.stops.size(); ++i) {
        slopengine::TextureGraphStop& stop = node.stops[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - iconButtonWidth() - 24.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f);
        if (ImGui::SliderFloat("##pos", &stop.position, 0.0f, 1.0f, "%.2f")) {
            editor.markDirty();
        }
        ImGui::SameLine();
        float rgba[4] = {stop.color.x, stop.color.y, stop.color.z, stop.color.w};
        if (ImGui::ColorEdit4("##stopcolor", rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            stop.color = {rgba[0], rgba[1], rgba[2], rgba[3]};
            editor.markDirty();
        }
        ImGui::SameLine();
        if (deleteButton(assets, "Remove stop")) {
            removedAny = true;
            removeIndex = i;
        }
        ImGui::PopID();
    }
    if (removedAny) {
        node.stops.erase(node.stops.begin() + static_cast<std::ptrdiff_t>(removeIndex));
        editor.markDirty();
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add stop")) {
        slopengine::TextureGraphStop stop{};
        stop.position = node.stops.empty() ? 0.0f : 1.0f;
        stop.color = node.stops.empty() ? Vector4{0.0f, 0.0f, 0.0f, 1.0f} : Vector4{1.0f, 1.0f, 1.0f, 1.0f};
        node.stops.push_back(stop);
        editor.markDirty();
    }
    if (!ImGui::IsAnyItemActive()) {
        std::stable_sort(node.stops.begin(), node.stops.end(), [](const slopengine::TextureGraphStop& a, const slopengine::TextureGraphStop& b) {
            return a.position < b.position;
        });
    }
}

void drawStackFields(Editor& editor, slopengine::AssetStore& assets, TextureGraphNode& node) {
    sizeField(editor, node, "Canvas size in pixels; 0 0 uses the first layer's size");
    ImGui::TextDisabled("Layers, bottom to top");
    bool removedAny = false;
    std::size_t removeIndex = 0;
    std::size_t moveIndex = 0;
    int moveDir = 0;
    for (std::size_t i = 0; i < node.layers.size(); ++i) {
        bool removed = false;
        int dir = 0;
        drawLayer(editor, assets, node, i, removed, dir);
        if (removed) {
            removedAny = true;
            removeIndex = i;
        }
        if (dir != 0) {
            moveIndex = i;
            moveDir = dir;
        }
        if (i + 1 < node.layers.size()) {
            ImGui::Separator();
        }
    }
    if (removedAny) {
        node.layers.erase(node.layers.begin() + static_cast<std::ptrdiff_t>(removeIndex));
        editor.markDirty();
    } else if (moveDir != 0) {
        const std::size_t other = moveDir < 0 ? moveIndex - 1 : moveIndex + 1;
        std::swap(node.layers[moveIndex], node.layers[other]);
        editor.markDirty();
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add layer")) {
        TextureGraphLayer layer{};
        layer.repeat = true;
        for (const TextureGraphNode& candidate : editor.doc.graph.nodes) {
            if (candidate.id != node.id && candidate.id != editor.doc.compositeId) {
                layer.source = candidate.id;
                break;
            }
        }
        node.layers.push_back(layer);
        editor.markDirty();
    }
}

void drawNode(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser, std::size_t index, bool& removed) {
    TextureGraph& graph = editor.doc.graph;
    TextureGraphNode& node = graph.nodes[index];
    ImGui::PushID(static_cast<int>(index));

    char header[192];
    std::snprintf(header, sizeof(header), "%s  [%s]###node", node.id.c_str(), slopengine::textureGraphNodeKindName(node.kind));
    const bool openHeader = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - iconButtonWidth());
    if (deleteButton(assets, "Delete node")) {
        if (isReferenced(graph, node.id)) {
            editor.setStatus("Node '" + node.id + "' is still used by a layer or output");
        } else {
            removed = true;
        }
    }
    if (!openHeader) {
        ImGui::PopID();
        return;
    }

    labeledField("Id");
    char idBuf[128] = {};
    std::snprintf(idBuf, sizeof(idBuf), "%s", node.id.c_str());
    ImGui::InputText("##id", idBuf, sizeof(idBuf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::string wanted = idBuf;
        if (wanted.empty()) {
            editor.setStatus("Node id cannot be empty");
        } else if (wanted != node.id && graph.findNode(wanted) != nullptr) {
            editor.setStatus("Node id '" + wanted + "' already exists");
        } else if (wanted != node.id) {
            renameNode(graph, node.id, wanted);
            node.id = wanted;
            editor.markDirty();
        }
    }

    labeledField("Kind");
    if (ImGui::BeginCombo("##kind", slopengine::textureGraphNodeKindName(node.kind))) {
        for (const TextureGraphNodeKind kind : kKinds) {
            if (ImGui::Selectable(slopengine::textureGraphNodeKindName(kind), kind == node.kind)) {
                node.kind = kind;
                if (kind == TextureGraphNodeKind::Stack || kind == TextureGraphNodeKind::Transform) {
                    node.width = 0;
                    node.height = 0;
                } else if (node.width == 0 || node.height == 0) {
                    node.width = 64;
                    node.height = 64;
                }
                editor.markDirty();
            }
        }
        ImGui::EndCombo();
    }

    switch (node.kind) {
    case TextureGraphNodeKind::Image:
        drawImageFields(editor, assets, picker, browser, node);
        break;
    case TextureGraphNodeKind::Color:
        sizeField(editor, node, nullptr);
        colorField(editor, "Color", "##color", node.color);
        break;
    case TextureGraphNodeKind::Checker: {
        sizeField(editor, node, nullptr);
        labeledField("Cells");
        int cells[2] = {node.cellsX, node.cellsY};
        if (ImGui::InputInt2("##cells", cells)) {
            node.cellsX = std::max(1, cells[0]);
            node.cellsY = std::max(1, cells[1]);
            editor.markDirty();
        }
        colorField(editor, "Color A", "##colora", node.color);
        colorField(editor, "Color B", "##colorb", node.colorB);
        break;
    }
    case TextureGraphNodeKind::Gradient:
        sizeField(editor, node, nullptr);
        labeledField("Angle");
        if (ImGui::SliderFloat("##angle", &node.angle, 0.0f, 360.0f, "%.0f deg")) {
            editor.markDirty();
        }
        colorField(editor, "Start", "##start", node.color);
        colorField(editor, "End", "##end", node.colorB);
        break;
    case TextureGraphNodeKind::Noise:
        sizeField(editor, node, nullptr);
        labeledField("Type");
        if (ImGui::BeginCombo("##noisetype", slopengine::textureGraphNoiseTypeName(node.noiseType))) {
            for (const slopengine::TextureGraphNoiseType type : kNoiseTypes) {
                if (ImGui::Selectable(slopengine::textureGraphNoiseTypeName(type), type == node.noiseType)) {
                    node.noiseType = type;
                    editor.markDirty();
                }
            }
            ImGui::EndCombo();
        }
        floatField(editor, "Scale", "##scale", node.scale, 0.1f, 1.0f, 256.0f, "%.0f cells");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Lattice cells across the image; whole numbers tile seamlessly");
        }
        labeledField("Octaves");
        if (ImGui::SliderInt("##octaves", &node.octaves, 1, 8)) {
            editor.markDirty();
        }
        floatField(editor, "Persistence", "##persistence", node.persistence, 0.01f, 0.0f, 1.0f, "%.2f");
        floatField(editor, "Lacunarity", "##lacunarity", node.lacunarity, 0.05f, 1.0f, 8.0f, "%.2f");
        labeledField("Seed");
        if (ImGui::InputInt("##seed", &node.seed)) {
            editor.markDirty();
        }
        colorField(editor, "High", "##high", node.color);
        colorField(editor, "Low", "##low", node.colorB);
        break;
    case TextureGraphNodeKind::Bricks: {
        sizeField(editor, node, nullptr);
        labeledField("Brick");
        int brick[2] = {node.brickWidth, node.brickHeight};
        if (ImGui::InputInt2("##brick", brick)) {
            node.brickWidth = std::max(1, brick[0]);
            node.brickHeight = std::max(1, brick[1]);
            editor.markDirty();
        }
        labeledField("Mortar");
        if (ImGui::InputInt("##mortar", &node.mortar)) {
            node.mortar = std::max(0, node.mortar);
            editor.markDirty();
        }
        floatField(editor, "Row offset", "##rowoffset", node.rowOffset, 0.01f, 0.0f, 1.0f, "%.2f");
        colorField(editor, "Brick", "##brickcolor", node.color);
        colorField(editor, "Mortar", "##mortarcolor", node.colorB);
        break;
    }
    case TextureGraphNodeKind::Stripes:
        sizeField(editor, node, nullptr);
        labeledField("Stripe");
        if (ImGui::InputInt("##stripe", &node.stripe)) {
            node.stripe = std::max(1, node.stripe);
            editor.markDirty();
        }
        labeledField("Angle");
        if (ImGui::SliderFloat("##angle", &node.angle, 0.0f, 360.0f, "%.0f deg")) {
            editor.markDirty();
        }
        colorField(editor, "Color A", "##colora", node.color);
        colorField(editor, "Color B", "##colorb", node.colorB);
        break;
    case TextureGraphNodeKind::GradientMap:
        inputField(editor, node);
        drawStops(editor, assets, node);
        break;
    case TextureGraphNodeKind::Transform: {
        inputField(editor, node);
        sizeField(editor, node, "Output size in pixels; 0 0 keeps the input size");
        labeledField("Scale");
        float scale[2] = {node.scaleX, node.scaleY};
        if (ImGui::DragFloat2("##xform-scale", scale, 0.01f, 0.05f, 16.0f, "%.2f")) {
            node.scaleX = scale[0];
            node.scaleY = scale[1];
            editor.markDirty();
        }
        labeledField("Rotate");
        if (ImGui::SliderFloat("##rotate", &node.angle, 0.0f, 360.0f, "%.0f deg")) {
            editor.markDirty();
        }
        labeledField("Flip");
        if (ImGui::Checkbox("H", &node.flipH)) {
            editor.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("V", &node.flipV)) {
            editor.markDirty();
        }
        break;
    }
    case TextureGraphNodeKind::Levels:
        inputField(editor, node);
        floatField(editor, "In black", "##inblack", node.inBlack, 0.01f, 0.0f, 1.0f, "%.2f");
        floatField(editor, "In white", "##inwhite", node.inWhite, 0.01f, 0.0f, 1.0f, "%.2f");
        floatField(editor, "Gamma", "##gamma", node.gamma, 0.01f, 0.1f, 5.0f, "%.2f");
        floatField(editor, "Out black", "##outblack", node.outBlack, 0.01f, 0.0f, 1.0f, "%.2f");
        floatField(editor, "Out white", "##outwhite", node.outWhite, 0.01f, 0.0f, 1.0f, "%.2f");
        break;
    case TextureGraphNodeKind::Hsv:
        inputField(editor, node);
        floatField(editor, "Hue", "##hue", node.hue, 1.0f, -180.0f, 180.0f, "%.0f deg");
        floatField(editor, "Saturation", "##sat", node.saturation, 0.01f, 0.0f, 4.0f, "%.2f");
        floatField(editor, "Value", "##val", node.value, 0.01f, 0.0f, 4.0f, "%.2f");
        break;
    case TextureGraphNodeKind::Invert:
        inputField(editor, node);
        break;
    case TextureGraphNodeKind::Blur:
        inputField(editor, node);
        floatField(editor, "Radius", "##radius", node.radius, 0.1f, 0.0f, 32.0f, "%.1f px");
        break;
    case TextureGraphNodeKind::Sharpen:
        inputField(editor, node);
        floatField(editor, "Amount", "##amount", node.amount, 0.01f, 0.0f, 8.0f, "%.2f");
        break;
    case TextureGraphNodeKind::Edges:
        inputField(editor, node);
        floatField(editor, "Gain", "##amount", node.amount, 0.01f, 0.0f, 8.0f, "%.2f");
        break;
    case TextureGraphNodeKind::Stack:
        drawStackFields(editor, assets, node);
        break;
    }

    ImGui::Spacing();
    ImGui::PopID();
}

void drawOutputs(Editor& editor, slopengine::AssetStore& assets) {
    TextureGraph& graph = editor.doc.graph;
    ImGui::SeparatorText("Outputs");
    ImGui::TextDisabled("Materials read \"albedo\" and \"emission\"");
    bool outputRemoved = false;
    std::size_t outputRemoveIndex = 0;
    for (std::size_t i = 0; i < graph.outputs.size(); ++i) {
        slopengine::TextureGraphOutput& output = graph.outputs[i];
        ImGui::PushID(static_cast<int>(i));
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float half = (ImGui::GetContentRegionAvail().x - iconButtonWidth() - spacing * 2.0f) * 0.5f;
        ImGui::SetNextItemWidth(half);
        char nameBuf[64] = {};
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", output.name.c_str());
        ImGui::InputText("##name", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string wanted = nameBuf;
            if (!wanted.empty() && wanted != output.name) {
                if (graph.findOutput(wanted) != nullptr) {
                    editor.setStatus("Output '" + wanted + "' already exists");
                } else {
                    output.name = wanted;
                    editor.markDirty();
                }
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        if (nodeCombo("##outsource", graph, output.source, "")) {
            editor.markDirty();
        }
        ImGui::SameLine();
        if (deleteButton(assets, "Remove output")) {
            outputRemoved = true;
            outputRemoveIndex = i;
        }
        ImGui::PopID();
    }
    if (outputRemoved) {
        graph.outputs.erase(graph.outputs.begin() + static_cast<std::ptrdiff_t>(outputRemoveIndex));
        editor.markDirty();
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add output")) {
        graph.outputs.push_back({nextOutputName(graph), editor.doc.compositeId});
        editor.markDirty();
    }
}

void drawCompositionTab(Editor& editor, slopengine::AssetStore& assets) {
    TextureGraph& graph = editor.doc.graph;
    TextureGraphNode* composite = graph.findNode(editor.doc.compositeId);
    if (composite == nullptr) {
        editor.ensureComposite();
        composite = graph.findNode(editor.doc.compositeId);
    }
    if (composite == nullptr) {
        ImGui::TextDisabled("No composite node.");
        return;
    }
    ImGui::PushID("composite");
    ImGui::TextDisabled("Composite node \"%s\": the stack every graph renders through", composite->id.c_str());
    drawStackFields(editor, assets, *composite);
    ImGui::PopID();
    ImGui::Spacing();
    drawOutputs(editor, assets);
}

void drawNodesTab(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser) {
    TextureGraph& graph = editor.doc.graph;
    ImGui::TextDisabled("Sources and filters the composition's layers reference");

    bool nodeRemoved = false;
    std::size_t nodeRemoveIndex = 0;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].id == editor.doc.compositeId) {
            continue;
        }
        bool removed = false;
        drawNode(editor, assets, picker, browser, i, removed);
        if (removed) {
            nodeRemoved = true;
            nodeRemoveIndex = i;
        }
    }
    if (nodeRemoved) {
        graph.nodes.erase(graph.nodes.begin() + static_cast<std::ptrdiff_t>(nodeRemoveIndex));
        editor.markDirty();
    }

    static int addKindIndex = 0;
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##addkind", slopengine::textureGraphNodeKindName(kKinds[addKindIndex]))) {
        for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kKinds)); ++i) {
            if (ImGui::Selectable(slopengine::textureGraphNodeKindName(kKinds[i]), i == addKindIndex)) {
                addKindIndex = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(assets, kIcons, "add", "Add node")) {
        TextureGraphNode node{};
        node.kind = kKinds[addKindIndex];
        node.id = uniqueNodeId(graph, slopengine::textureGraphNodeKindName(node.kind));
        if (node.kind == TextureGraphNodeKind::Stack || node.kind == TextureGraphNodeKind::Transform) {
            node.width = 0;
            node.height = 0;
        }
        if (slopengine::textureGraphKindHasInput(node.kind)) {
            for (const TextureGraphNode& candidate : graph.nodes) {
                if (candidate.id != editor.doc.compositeId) {
                    node.input = candidate.id;
                }
            }
        }
        if (node.kind == TextureGraphNodeKind::GradientMap) {
            node.stops.push_back({0.0f, {0.0f, 0.0f, 0.0f, 1.0f}});
            node.stops.push_back({1.0f, {1.0f, 1.0f, 1.0f, 1.0f}});
        }
        graph.nodes.push_back(node);
        editor.markDirty();
    }
}

} // namespace

void drawGraphInspector(Editor& editor, slopengine::AssetStore& assets, AssetPicker& picker, const Browser& browser) {
    ImGui::TextDisabled("%s", editor.doc.virtualPath.c_str());
    if (!editor.doc.problem.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", editor.doc.problem.c_str());
    }

    if (slopengine::slopuiBeginTabBar("##graphTabs")) {
        if (slopengine::slopuiBeginTabItem("Composition")) {
            if (slopengine::slopuiBeginChild("##compositionTab", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
                drawCompositionTab(editor, assets);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (slopengine::slopuiBeginTabItem("Nodes")) {
            if (slopengine::slopuiBeginChild("##nodesTab", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
                drawNodesTab(editor, assets, picker, browser);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        slopengine::slopuiEndTabBar();
    }
}

}
