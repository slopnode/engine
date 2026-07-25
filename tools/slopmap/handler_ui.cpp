#include "handler_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace slopmap {

namespace {

slopengine::HandlerArgValue defaultForType(slopengine::HandlerArgType type) {
    slopengine::HandlerArgValue value{};
    value.type = type;
    if (type == slopengine::HandlerArgType::Color || type == slopengine::HandlerArgType::Vec3) {
        value.v = {1.0f, 1.0f, 1.0f};
    }
    return value;
}

void ensureCatalogArgs(slopengine::HandlerBinding& binding) {
    const slopengine::MapHandlerDef* def = slopengine::mapHandlerRegistry().find(binding.id);
    if (def == nullptr) {
        return;
    }
    for (const slopengine::MapHandlerParam& param : def->params) {
        if (slopengine::findHandlerArg(binding, param.name) != nullptr) {
            continue;
        }
        slopengine::HandlerArg arg{};
        arg.name = param.name;
        arg.value = param.hasDefault ? param.defaultValue : defaultForType(param.type);
        arg.value.type = param.type;
        binding.args.push_back(std::move(arg));
    }
    for (slopengine::HandlerArg& arg : binding.args) {
        for (const slopengine::MapHandlerParam& param : def->params) {
            if (param.name == arg.name) {
                arg.value.type = param.type;
                break;
            }
        }
    }
}

slopengine::HandlerArg* ensureArg(
    slopengine::HandlerBinding& binding,
    const slopengine::MapHandlerParam& param) {
    if (slopengine::HandlerArgValue* existing = slopengine::findHandlerArg(binding, param.name)) {
        existing->type = param.type;
        for (slopengine::HandlerArg& arg : binding.args) {
            if (arg.name == param.name) {
                return &arg;
            }
        }
    }
    slopengine::HandlerArg arg{};
    arg.name = param.name;
    arg.value = param.hasDefault ? param.defaultValue : defaultForType(param.type);
    arg.value.type = param.type;
    binding.args.push_back(std::move(arg));
    return &binding.args.back();
}

std::vector<std::string> collectRefIds(Editor& editor, slopengine::HandlerArgType type) {
    std::vector<std::string> ids;
    const EditorDocument& doc = editor.doc();
    switch (type) {
    case slopengine::HandlerArgType::Thing:
        ids.reserve(doc.things.size());
        for (const slopengine::Thing& thing : doc.things) {
            if (!thing.id.empty()) {
                ids.push_back(thing.id);
            }
        }
        break;
    case slopengine::HandlerArgType::Brush:
        ids.reserve(doc.brushes.size());
        for (const slopengine::Brush& brush : doc.brushes) {
            if (!brush.id.empty()) {
                ids.push_back(brush.id);
            }
        }
        break;
    case slopengine::HandlerArgType::Face:
        for (const slopengine::Brush& brush : doc.brushes) {
            for (std::size_t i = 0; i < brush.faces.size(); ++i) {
                ids.push_back(resolvedFaceId(brush, brush.faces[i], i));
            }
        }
        break;
    default:
        break;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool drawRefIdParam(Editor& editor, slopengine::HandlerArg& arg) {
    ImGui::PushID(arg.name.c_str());
    bool changed = false;
    const std::vector<std::string> ids = collectRefIds(editor, arg.value.type);
    const char* preview = arg.value.s.empty() ? "(none)" : arg.value.s.c_str();
    if (ImGui::BeginCombo(arg.name.c_str(), preview)) {
        if (ImGui::Selectable("(none)", arg.value.s.empty())) {
            arg.value.s.clear();
            changed = true;
        }
        if (!arg.value.s.empty() &&
            std::find(ids.begin(), ids.end(), arg.value.s) == ids.end()) {
            if (ImGui::Selectable(arg.value.s.c_str(), true)) {
                changed = false;
            }
        }
        for (const std::string& id : ids) {
            const bool selected = arg.value.s == id;
            if (ImGui::Selectable(id.c_str(), selected)) {
                arg.value.s = id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", arg.value.s.c_str());
    if (ImGui::InputText("##ref", buf, sizeof(buf))) {
        arg.value.s = buf;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool drawParam(Editor& editor, slopengine::HandlerArg& arg) {
    switch (arg.value.type) {
    case slopengine::HandlerArgType::Int: {
        ImGui::PushID(arg.name.c_str());
        int v = arg.value.i;
        const bool changed = ImGui::InputInt(arg.name.c_str(), &v);
        if (changed) {
            arg.value.i = v;
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::Float: {
        ImGui::PushID(arg.name.c_str());
        float v = arg.value.f;
        const bool changed = ImGui::DragFloat(arg.name.c_str(), &v, 0.05f);
        if (changed) {
            arg.value.f = v;
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::Bool: {
        ImGui::PushID(arg.name.c_str());
        bool v = arg.value.b;
        const bool changed = ImGui::Checkbox(arg.name.c_str(), &v);
        if (changed) {
            arg.value.b = v;
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::String: {
        ImGui::PushID(arg.name.c_str());
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", arg.value.s.c_str());
        const bool changed = ImGui::InputText(arg.name.c_str(), buf, sizeof(buf));
        if (changed) {
            arg.value.s = buf;
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::Color: {
        ImGui::PushID(arg.name.c_str());
        float c[3] = {arg.value.v.x, arg.value.v.y, arg.value.v.z};
        const bool changed = ImGui::ColorEdit3(arg.name.c_str(), c);
        if (changed) {
            arg.value.v = {c[0], c[1], c[2]};
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::Vec3: {
        ImGui::PushID(arg.name.c_str());
        float v[3] = {arg.value.v.x, arg.value.v.y, arg.value.v.z};
        const bool changed = ImGui::DragFloat3(arg.name.c_str(), v, 0.05f);
        if (changed) {
            arg.value.v = {v[0], v[1], v[2]};
        }
        ImGui::PopID();
        return changed;
    }
    case slopengine::HandlerArgType::Thing:
    case slopengine::HandlerArgType::Brush:
    case slopengine::HandlerArgType::Face:
        return drawRefIdParam(editor, arg);
    }
    return false;
}

const slopengine::MapHandlerDef* defForKind(
    const slopengine::HandlerBinding& binding,
    slopengine::MapHandlerKind kind) {
    const slopengine::MapHandlerDef* def = slopengine::mapHandlerRegistry().find(binding.id);
    if (def == nullptr) {
        return nullptr;
    }
    if (!slopengine::mapHandlerRegistry().allowsKind(binding.id, kind)) {
        return nullptr;
    }
    return def;
}

} // namespace

std::string resolvedFaceId(
    const slopengine::Brush& brush,
    const slopengine::BrushFace& face,
    std::size_t faceIndex) {
    if (!face.id.empty()) {
        return face.id;
    }
    return brush.id + "/" + std::to_string(faceIndex);
}

bool drawHandlerBindingEditor(
    Editor& editor,
    const char* label,
    const char* imguiId,
    slopengine::MapHandlerKind kind,
    const std::optional<slopengine::HandlerBinding>& common,
    const std::function<void(slopengine::HandlerBinding&)>& apply) {
    bool changed = false;
    ImGui::PushID(imguiId);

    const bool mixed = !common.has_value();
    slopengine::HandlerBinding binding = common.value_or(slopengine::HandlerBinding{});
    const auto catalog = slopengine::mapHandlerRegistry().handlersForKind(kind);
    const slopengine::MapHandlerDef* selectedDef = defForKind(binding, kind);

    const char* preview = mixed                         ? "(mixed)"
        : (binding.empty() || selectedDef == nullptr) ? "(none)"
                                                        : selectedDef->label.c_str();

    const char* comboLabel = (label != nullptr && label[0] != '\0') ? label : "##handler";
    if (mixed) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.65f);
    }
    if (ImGui::BeginCombo(comboLabel, preview)) {
        if (ImGui::Selectable("(none)", !mixed && (binding.empty() || selectedDef == nullptr))) {
            binding.clear();
            apply(binding);
            changed = true;
            editor.statusMessage = "Cleared handler";
        }
        for (const slopengine::MapHandlerDef* def : catalog) {
            const bool selected = !mixed && selectedDef != nullptr && binding.id == def->id;
            if (ImGui::Selectable(def->label.c_str(), selected)) {
                binding.id = def->id;
                binding.args.clear();
                ensureCatalogArgs(binding);
                apply(binding);
                changed = true;
                editor.statusMessage = std::string("Set handler: ") + def->id;
            }
        }
        ImGui::EndCombo();
    }
    if (mixed) {
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("mixed values");
        }
    }

    if (!mixed && selectedDef != nullptr) {
        ensureCatalogArgs(binding);
        bool argsChanged = false;
        for (const slopengine::MapHandlerParam& param : selectedDef->params) {
            slopengine::HandlerArg* arg = ensureArg(binding, param);
            if (arg != nullptr && drawParam(editor, *arg)) {
                argsChanged = true;
            }
        }
        if (argsChanged) {
            apply(binding);
            changed = true;
            editor.statusMessage = std::string("Updated args for ") + binding.id;
        }
    }

    ImGui::PopID();
    return changed;
}

}
