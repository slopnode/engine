#include "ui/slopui_editor.hpp"

#include "assets/asset_store.hpp"
#include "core/package.hpp"
#include "ui/slopui_theme.hpp"
#include "ui/ui_state.hpp"

#include <imgui.h>
#include <raylib.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <string>

namespace slopengine {

namespace {

bool colorEdit(const char* label, ThemeColor& color) {
    float rgb[3] = {color.r, color.g, color.b};
    if (!ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs)) {
        return false;
    }
    color.r = rgb[0];
    color.g = rgb[1];
    color.b = rgb[2];
    return true;
}

bool vec2Edit(const char* label, ImVec2& value, float lo, float hi) {
    float v[2] = {value.x, value.y};
    if (!ImGui::SliderFloat2(label, v, lo, hi, "%.0f")) {
        return false;
    }
    value = ImVec2{v[0], v[1]};
    return true;
}

bool fontSlotCombo(const char* label, ThemeFontSlot& slot) {
    const char* names[] = {
        themeFontSlotName(ThemeFontSlot::Normal),
        themeFontSlotName(ThemeFontSlot::Bold),
        themeFontSlotName(ThemeFontSlot::Heading),
        themeFontSlotName(ThemeFontSlot::Mono),
    };
    int index = static_cast<int>(slot);
    if (!ImGui::Combo(label, &index, names, IM_ARRAYSIZE(names))) {
        return false;
    }
    slot = static_cast<ThemeFontSlot>(index);
    return true;
}

struct EditorState {
    std::array<char, 256> fontPaths[static_cast<std::size_t>(ThemeFontSlot::Count)]{};
    std::array<char, 128> pauseTitle{};
    bool buffersReady = false;
    bool overwrite = false;
    std::string status;
};

EditorState& editorState() {
    static EditorState state;
    return state;
}

void syncBuffers(EditorState& state, const SlopUiTheme& theme) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
        std::snprintf(state.fontPaths[i].data(), state.fontPaths[i].size(), "%s", theme.fonts[i].path.c_str());
    }
    std::snprintf(state.pauseTitle.data(), state.pauseTitle.size(), "%s", theme.chrome.pauseTitle.c_str());
    state.buffersReady = true;
}

std::filesystem::path exportPath(const AssetStore& assets) {
    const std::vector<Package>& packages = assets.packages();
    if (packages.size() < 2) {
        return {};
    }
    return packages[1].root() / "data" / "theme.s7";
}

} // namespace

void drawThemeEditor(flecs::world world, AssetStore& assets, DebugUiState& debugUi) {
    if (!debugUi.themeEditorOpen || !world.has<SlopUiTheme>() || !world.has<SlopUiFonts>()) {
        return;
    }

    ImGui::SetNextWindowSize({420.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Theme Editor", &debugUi.themeEditorOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    SlopUiTheme& theme = world.get_mut<SlopUiTheme>();
    SlopUiFonts& fonts = world.get_mut<SlopUiFonts>();
    EditorState& state = editorState();
    if (!state.buffersReady || ImGui::IsWindowAppearing()) {
        syncBuffers(state, theme);
    }

    bool changed = false;
    bool fontsChanged = false;

    if (ImGui::CollapsingHeader("Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= colorEdit("bg", theme.bg);
        changed |= colorEdit("surface", theme.surface);
        changed |= colorEdit("accent", theme.accent);
        changed |= colorEdit("text", theme.text);
        changed |= colorEdit("text-dim", theme.textDim);
        changed |= colorEdit("danger", theme.danger);
    }

    if (ImGui::CollapsingHeader("Fonts")) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(ThemeFontSlot::Count); ++i) {
            ThemeFont& font = theme.fonts[i];
            const char* name = themeFontSlotName(static_cast<ThemeFontSlot>(i));
            ImGui::PushID(name);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText(
                    "path", state.fontPaths[i].data(), state.fontPaths[i].size(),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                font.path = state.fontPaths[i].data();
                fontsChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name);
            float size = font.size;
            if (ImGui::SliderFloat("size", &size, 8.0f, 64.0f, "%.0f")) {
                font.size = size;
                fontsChanged = true;
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Metrics")) {
        changed |= ImGui::SliderFloat("rounding", &theme.rounding, 0.0f, 16.0f, "%.0f");
        changed |= ImGui::SliderFloat("border", &theme.border, 0.0f, 4.0f, "%.0f");
        changed |= ImGui::SliderFloat("frame-border", &theme.frameBorder, 0.0f, 4.0f, "%.0f");
        changed |= ImGui::SliderFloat("bevel", &theme.bevel, 0.0f, 4.0f, "%.0f");
        changed |= vec2Edit("padding", theme.padding, 0.0f, 32.0f);
        changed |= vec2Edit("frame-padding", theme.framePadding, 0.0f, 24.0f);
        changed |= vec2Edit("spacing", theme.spacing, 0.0f, 24.0f);
        changed |= ImGui::SliderFloat("button-height", &theme.buttonHeight, 16.0f, 64.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Menu Bar")) {
        changed |= ImGui::SliderFloat("height", &theme.menuBar.height, 16.0f, 64.0f, "%.0f");
        changed |= fontSlotCombo("font##menubar", theme.menuBar.font);
        changed |= ImGui::Checkbox("icons", &theme.menuBar.icons);
        changed |= ImGui::Checkbox("version-label", &theme.menuBar.versionLabel);
    }

    if (ImGui::CollapsingHeader("Window")) {
        changed |= fontSlotCombo("title-font", theme.window.titleFont);
        int align = theme.window.titleCentered ? 0 : 1;
        const char* aligns[] = {"center", "left"};
        if (ImGui::Combo("title-align", &align, aligns, IM_ARRAYSIZE(aligns))) {
            theme.window.titleCentered = align == 0;
            changed = true;
        }
        changed |= ImGui::Checkbox("draggable", &theme.window.draggable);
    }

    if (ImGui::CollapsingHeader("Chrome")) {
        if (ImGui::InputText(
                "pause-title", state.pauseTitle.data(), state.pauseTitle.size(),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
            theme.chrome.pauseTitle = state.pauseTitle.data();
        }
        changed |= ImGui::SliderFloat("pause-dim", &theme.chrome.pauseDim, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("pause-width", &theme.chrome.pauseWidth, 200.0f, 800.0f, "%.0f");
        changed |= ImGui::SliderFloat("sound volume", &theme.sounds.volume, 0.0f, 1.0f, "%.2f");
    }

    ImGui::TextDisabled("Gradients, skins and sounds are edited in theme.s7 and kept on export.");
    ImGui::Separator();

    if (ImGui::Button("Reload From Packages")) {
        if (world.has<SlopUiThemeReload>()) {
            world.get_mut<SlopUiThemeReload>().pending = true;
        }
        state.buffersReady = false;
        state.status = "Reloading theme files on next frame.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset To Engine Default")) {
        theme = defaultSlopUiTheme();
        syncBuffers(state, theme);
        fontsChanged = true;
        state.status = "Theme reset to engine default.";
    }

    const std::filesystem::path path = exportPath(assets);
    if (path.empty()) {
        ImGui::TextDisabled("No base package mounted; export unavailable.");
    } else {
        const std::string pathText = path.string();
        ImGui::TextWrapped("Export to: %s", pathText.c_str());
        const bool exists = std::filesystem::exists(path);
        if (exists) {
            ImGui::Checkbox("Overwrite existing theme.s7", &state.overwrite);
        }
        const bool allowed = !exists || state.overwrite;
        if (!allowed) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Export Theme")) {
            state.status = exportSlopUiTheme(theme, path) ? "Exported " + pathText : "Export failed, see log.";
        }
        if (!allowed) {
            ImGui::EndDisabled();
        }
    }
    if (!state.status.empty()) {
        ImGui::TextWrapped("%s", state.status.c_str());
    }

    ImGui::End();

    if (fontsChanged) {
        applySlopUiTheme(theme, assets, fonts);
    } else if (changed) {
        applySlopUiStyle(theme);
    }
}

}
