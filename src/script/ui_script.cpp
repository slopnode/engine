#include "script/ui_script.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include "game/game_state.hpp"
#include "input/input_context.hpp"
#include "script/hook_registry.hpp"
#include "script/scheme_call.hpp"
#include "assets/asset_services.hpp"
#include "script/script_context.hpp"
#include "ui/slopui_theme.hpp"
#include "ui/slopui_widgets.hpp"
#include "ui/slopui_world.hpp"

#include "imgui.h"

#include <s7.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace slopengine {

namespace {

flecs::world* g_uiWorld = nullptr;
PackageUiDrawSlot g_uiSlot = PackageUiDrawSlot::None;
std::unordered_map<std::string, std::string> g_inputTextBuffers;
std::unordered_map<std::string, std::vector<char>> g_inputTextScratch;

bool inMenuItemSlot() {
    return g_uiSlot == PackageUiDrawSlot::FileMenu || g_uiSlot == PackageUiDrawSlot::MenuBar ||
           g_uiSlot == PackageUiDrawSlot::PauseMenu || g_uiSlot == PackageUiDrawSlot::DebugMenu;
}

bool inMenuSlot() {
    return g_uiSlot == PackageUiDrawSlot::FileMenu || g_uiSlot == PackageUiDrawSlot::MenuBar ||
           g_uiSlot == PackageUiDrawSlot::DebugMenu;
}

const SlopUiTheme* uiTheme() {
    if (g_uiWorld == nullptr || !g_uiWorld->has<SlopUiTheme>()) {
        return nullptr;
    }
    return &g_uiWorld->get<SlopUiTheme>();
}

const SlopUiFonts* uiFonts() {
    if (g_uiWorld == nullptr || !g_uiWorld->has<SlopUiFonts>()) {
        return nullptr;
    }
    return &g_uiWorld->get<SlopUiFonts>();
}

AssetStore* uiAssets() {
    if (g_uiWorld == nullptr || !g_uiWorld->has<AssetServices>()) {
        return nullptr;
    }
    return g_uiWorld->get_mut<AssetServices>().store;
}

void uiConfirmSound() {
    if (g_uiWorld != nullptr) {
        playUiSound(*g_uiWorld, UiSoundKind::Confirm);
    }
}

bool contextContains(InputContext context) {
    if (g_uiWorld == nullptr || !g_uiWorld->has<InputContextStack>()) {
        return false;
    }
    return g_uiWorld->get<InputContextStack>().contains(context);
}

s7_pointer g_playing_p(s7_scheme* sc, s7_pointer) {
    if (g_uiWorld == nullptr) {
        return s7_f(sc);
    }
    return s7_make_boolean(sc, isPlaying(*g_uiWorld));
}

s7_pointer g_main_menu_p(s7_scheme* sc, s7_pointer) {
    return s7_make_boolean(sc, contextContains(InputContext::MainMenu));
}

s7_pointer g_pause_menu_p(s7_scheme* sc, s7_pointer) {
    return s7_make_boolean(sc, contextContains(InputContext::PauseMenu));
}

constexpr SchemeBinding kGameStateBindings[] = {
    {"playing?", g_playing_p, 0, 0, false, "(playing?)", ScriptCap::ReadWorld},
    {"main-menu?", g_main_menu_p, 0, 0, false, "(main-menu?)", ScriptCap::ReadWorld},
    {"pause-menu?", g_pause_menu_p, 0, 0, false, "(pause-menu?)", ScriptCap::ReadWorld},
};

s7_pointer g_ui_text(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-text", args);
    ImGui::TextUnformatted(in.string("string"));
    return s7_t(sc);
}

s7_pointer g_ui_separator(s7_scheme* sc, s7_pointer) {
    ImGui::Separator();
    return s7_t(sc);
}

s7_pointer g_ui_same_line(s7_scheme* sc, s7_pointer) {
    ImGui::SameLine();
    return s7_t(sc);
}

s7_pointer g_ui_button(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-button", args);
    const char* label = in.string("string");
    const SlopUiTheme* theme = uiTheme();
    const SlopUiFonts* fonts = uiFonts();
    bool pressed = false;
    if (theme != nullptr && fonts != nullptr) {
        const ImVec2 size = g_uiSlot == PackageUiDrawSlot::PauseMenu
            ? ImVec2{-1.0f, theme->buttonHeight}
            : ImVec2{0.0f, 0.0f};
        pressed = themedButton(*theme, *fonts, uiAssets(), label, size);
    } else {
        pressed = ImGui::Button(label);
    }
    if (pressed) {
        uiConfirmSound();
    }
    return s7_make_boolean(sc, pressed);
}

s7_pointer g_ui_menu_item(s7_scheme* sc, s7_pointer args) {
    if (!inMenuItemSlot()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "ui-menu-item", args);
    const char* label = in.string("string");
    const bool enabled = in.more() ? in.truthy("boolean") : true;
    const bool clicked = ImGui::MenuItem(label, nullptr, false, enabled);
    if (clicked) {
        uiConfirmSound();
    }
    return s7_make_boolean(sc, clicked);
}

s7_pointer g_ui_menu_check(s7_scheme* sc, s7_pointer args) {
    if (!inMenuItemSlot()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "ui-menu-check", args);
    const char* label = in.string("string");
    bool selected = in.boolean("boolean");
    if (ImGui::MenuItem(label, nullptr, &selected, true)) {
        uiConfirmSound();
    }
    return s7_make_boolean(sc, selected);
}

s7_pointer g_ui_selectable(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-selectable", args);
    const char* label = in.string("string");
    const bool selected = in.more() && in.truthy("boolean");
    const bool clicked = ImGui::Selectable(label, selected);
    if (clicked) {
        uiConfirmSound();
    }
    return s7_make_boolean(sc, clicked);
}

s7_pointer g_ui_begin(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-begin", args);
    const char* title = in.string("string");
    const SlopUiTheme* theme = uiTheme();
    const SlopUiFonts* fonts = uiFonts();
    if (theme != nullptr && fonts != nullptr) {
        return s7_make_boolean(sc, beginThemedWindow(*theme, *fonts, uiAssets(), title));
    }
    const bool open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse);
    return s7_make_boolean(sc, open);
}

s7_pointer g_ui_end(s7_scheme* sc, s7_pointer) {
    ImGui::End();
    return s7_t(sc);
}

s7_pointer g_ui_begin_child(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-begin-child", args);
    const char* id = in.string("string");
    const float height = in.optRealf(0.0f);
    const bool open = slopuiBeginChild(id, ImVec2{0.0f, height}, ImGuiChildFlags_Borders);
    return s7_make_boolean(sc, open);
}

s7_pointer g_ui_end_child(s7_scheme* sc, s7_pointer) {
    ImGui::EndChild();
    return s7_t(sc);
}

s7_pointer g_ui_set_next_window_size(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-set-next-window-size", args);
    const float w = in.realf("w h numbers");
    const float h = in.realf("w h numbers");
    ImGui::SetNextWindowSize({w, h}, ImGuiCond_FirstUseEver);
    return s7_t(sc);
}

s7_pointer g_ui_center_next_window(s7_scheme* sc, s7_pointer) {
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        {0.5f, 0.5f});
    return s7_t(sc);
}

s7_pointer g_ui_input_text(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-input-text", args);
    const std::string id = in.string("id initial-string");
    const std::string initial = in.string("id initial-string");
    auto& stored = g_inputTextBuffers[id];
    if (stored.empty() && !initial.empty()) {
        stored = initial;
    }
    auto& scratch = g_inputTextScratch[id];
    if (scratch.size() < 256) {
        scratch.assign(256, '\0');
    }
    std::snprintf(scratch.data(), scratch.size(), "%s", stored.c_str());
    ImGui::PushID(id.c_str());
    if (ImGui::InputText("##text", scratch.data(), scratch.size())) {
        stored = scratch.data();
    }
    ImGui::PopID();
    return s7_make_string(sc, stored.c_str());
}

s7_pointer g_ui_input_text_set(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-input-text-set", args);
    const char* id = in.string("id string");
    const char* text = in.string("id string");
    g_inputTextBuffers[id] = text;
    return s7_t(sc);
}

s7_pointer g_ui_begin_combo(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-begin-combo", args);
    const char* id = in.string("id preview");
    const char* preview = in.string("id preview");
    ImGui::PushID(id);
    const bool open = ImGui::BeginCombo("##combo", preview);
    if (!open) {
        ImGui::PopID();
    }
    return s7_make_boolean(sc, open);
}

s7_pointer g_ui_combo_item(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-combo-item", args);
    const char* label = in.string("string");
    const bool selected = in.more() && in.truthy("boolean");
    const bool clicked = ImGui::Selectable(label, selected);
    if (clicked) {
        uiConfirmSound();
    }
    if (selected) {
        ImGui::SetItemDefaultFocus();
    }
    return s7_make_boolean(sc, clicked);
}

s7_pointer g_ui_end_combo(s7_scheme* sc, s7_pointer) {
    ImGui::EndCombo();
    ImGui::PopID();
    return s7_t(sc);
}

s7_pointer g_ui_indent(s7_scheme* sc, s7_pointer) {
    ImGui::Indent();
    return s7_t(sc);
}

s7_pointer g_ui_unindent(s7_scheme* sc, s7_pointer) {
    ImGui::Unindent();
    return s7_t(sc);
}

s7_pointer g_ui_push_font(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-push-font", args);
    s7_pointer cell = in.peek();
    const char* name = in.name("font slot normal|bold|heading|mono");
    ThemeFontSlot slot;
    if (!parseThemeFontSlot(name, slot)) {
        return in.wrongTypeAt(1, cell, "font slot normal|bold|heading|mono");
    }
    const SlopUiFonts* fonts = uiFonts();
    if (fonts == nullptr) {
        ImGui::PushFont(nullptr, 0.0f);
        return s7_t(sc);
    }
    pushThemeFont(*fonts, slot);
    return s7_t(sc);
}

s7_pointer g_ui_pop_font(s7_scheme* sc, s7_pointer) {
    ImGui::PopFont();
    return s7_t(sc);
}

constexpr SchemeBinding kWidgetBindings[] = {
    {"ui-text", g_ui_text, 1, 0, false, "(ui-text str)", ScriptCap::UiDraw},
    {"ui-separator", g_ui_separator, 0, 0, false, "(ui-separator)", ScriptCap::UiDraw},
    {"ui-same-line", g_ui_same_line, 0, 0, false, "(ui-same-line)", ScriptCap::UiDraw},
    {"ui-button", g_ui_button, 1, 0, false, "(ui-button label)", ScriptCap::UiDraw},
    {"ui-menu-item", g_ui_menu_item, 1, 1, false, "(ui-menu-item label [enabled?])", ScriptCap::UiDraw},
    {"ui-menu-check", g_ui_menu_check, 2, 0, false, "(ui-menu-check label selected?)", ScriptCap::UiDraw},
    {"ui-selectable", g_ui_selectable, 1, 1, false, "(ui-selectable label [selected?])", ScriptCap::UiDraw},
    {"ui-begin", g_ui_begin, 1, 0, false, "(ui-begin title)", ScriptCap::UiDraw},
    {"ui-end", g_ui_end, 0, 0, false, "(ui-end)", ScriptCap::UiDraw},
    {"ui-begin-child", g_ui_begin_child, 1, 1, false, "(ui-begin-child id [height])", ScriptCap::UiDraw},
    {"ui-end-child", g_ui_end_child, 0, 0, false, "(ui-end-child)", ScriptCap::UiDraw},
    {"ui-set-next-window-size", g_ui_set_next_window_size, 2, 0, false, "(ui-set-next-window-size w h)", ScriptCap::UiDraw},
    {"ui-center-next-window", g_ui_center_next_window, 0, 0, false, "(ui-center-next-window)", ScriptCap::UiDraw},
    {"ui-input-text", g_ui_input_text, 2, 0, false, "(ui-input-text id initial)", ScriptCap::UiDraw},
    {"ui-input-text-set", g_ui_input_text_set, 2, 0, false, "(ui-input-text-set id str)", ScriptCap::UiDraw},
    {"ui-begin-combo", g_ui_begin_combo, 2, 0, false, "(ui-begin-combo id preview)", ScriptCap::UiDraw},
    {"ui-combo-item", g_ui_combo_item, 1, 1, false, "(ui-combo-item label [selected?])", ScriptCap::UiDraw},
    {"ui-end-combo", g_ui_end_combo, 0, 0, false, "(ui-end-combo)", ScriptCap::UiDraw},
    {"ui-indent", g_ui_indent, 0, 0, false, "(ui-indent)", ScriptCap::UiDraw},
    {"ui-unindent", g_ui_unindent, 0, 0, false, "(ui-unindent)", ScriptCap::UiDraw},
    {"ui-push-font", g_ui_push_font, 1, 0, false, "(ui-push-font slot)", ScriptCap::UiDraw},
    {"ui-pop-font", g_ui_pop_font, 0, 0, false, "(ui-pop-font)", ScriptCap::UiDraw},
};

s7_pointer textInSlot(s7_scheme* sc, s7_pointer args, const char* fn, ThemeFontSlot slot) {
    ArgReader in(sc, fn, args);
    const char* text = in.string("string");
    const SlopUiFonts* fonts = uiFonts();
    if (fonts != nullptr) {
        pushThemeFont(*fonts, slot);
    }
    ImGui::TextUnformatted(text);
    if (fonts != nullptr) {
        ImGui::PopFont();
    }
    return s7_t(sc);
}

s7_pointer g_ui_heading(s7_scheme* sc, s7_pointer args) {
    return textInSlot(sc, args, "ui-heading", ThemeFontSlot::Heading);
}

s7_pointer g_ui_mono_text(s7_scheme* sc, s7_pointer args) {
    return textInSlot(sc, args, "ui-mono-text", ThemeFontSlot::Mono);
}

s7_pointer g_ui_text_dim(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-text-dim", args);
    ImGui::TextDisabled("%s", in.string("string"));
    return s7_t(sc);
}

s7_pointer g_ui_spacer(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-spacer", args);
    float height = ImGui::GetStyle().ItemSpacing.y;
    const float requested = in.optRealf(0.0f);
    if (requested > 0.0f) {
        height = requested;
    }
    ImGui::Dummy(ImVec2{0.0f, height});
    return s7_t(sc);
}

s7_pointer g_ui_primary_button(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-primary-button", args);
    const char* label = in.string("string");
    const SlopUiTheme* theme = uiTheme();
    const SlopUiFonts* fonts = uiFonts();
    bool clicked = false;
    if (theme == nullptr || fonts == nullptr) {
        clicked = ImGui::Button(label);
    } else {
        clicked = themedButton(
            *theme, *fonts, uiAssets(), label, ImVec2{-1.0f, theme->buttonHeight}, true);
    }
    if (clicked) {
        uiConfirmSound();
    }
    return s7_make_boolean(sc, clicked);
}

s7_pointer g_ui_begin_panel(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "ui-begin-panel", args);
    const char* id = in.string("string");
    const float height = in.optRealf(0.0f);
    const SlopUiTheme* theme = uiTheme();
    if (theme == nullptr) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
        ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
        if (height <= 0.0f) {
            flags |= ImGuiChildFlags_AutoResizeY;
        }
        const bool open = ImGui::BeginChild(id, ImVec2{0.0f, height}, flags);
        return s7_make_boolean(sc, open);
    }
    return s7_make_boolean(sc, beginThemedPanel(*theme, uiAssets(), id, height));
}

s7_pointer g_ui_end_panel(s7_scheme* sc, s7_pointer) {
    endThemedPanel();
    return s7_t(sc);
}

s7_pointer g_ui_begin_menu(s7_scheme* sc, s7_pointer args) {
    if (!inMenuSlot()) {
        return s7_f(sc);
    }
    ArgReader in(sc, "ui-begin-menu", args);
    const char* label = in.string("string");
    const bool enabled = in.more() ? in.truthy("boolean") : true;
    return s7_make_boolean(sc, ImGui::BeginMenu(label, enabled));
}

s7_pointer g_ui_end_menu(s7_scheme* sc, s7_pointer) {
    ImGui::EndMenu();
    return s7_t(sc);
}

constexpr SchemeBinding kThemedBindings[] = {
    {"ui-heading", g_ui_heading, 1, 0, false, "(ui-heading str)", ScriptCap::UiDraw},
    {"ui-text-dim", g_ui_text_dim, 1, 0, false, "(ui-text-dim str)", ScriptCap::UiDraw},
    {"ui-mono-text", g_ui_mono_text, 1, 0, false, "(ui-mono-text str)", ScriptCap::UiDraw},
    {"ui-spacer", g_ui_spacer, 0, 1, false, "(ui-spacer [height])", ScriptCap::UiDraw},
    {"ui-primary-button", g_ui_primary_button, 1, 0, false, "(ui-primary-button label)", ScriptCap::UiDraw},
    {"ui-begin-panel", g_ui_begin_panel, 1, 1, false, "(ui-begin-panel id [height])", ScriptCap::UiDraw},
    {"ui-end-panel", g_ui_end_panel, 0, 0, false, "(ui-end-panel)", ScriptCap::UiDraw},
    {"ui-begin-menu", g_ui_begin_menu, 1, 1, false, "(ui-begin-menu label [enabled?])", ScriptCap::UiDraw},
    {"ui-end-menu", g_ui_end_menu, 0, 0, false, "(ui-end-menu)", ScriptCap::UiDraw},
};

void callNamedHook(flecs::world& world, const char* name) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    callHook(world.get<ScriptContext>().scheme, name, ScriptScope::Ui);
}

} // namespace

void bindUiApi(flecs::world& world, s7_scheme* scheme) {
    g_uiWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    registerBindings(scheme, kGameStateBindings);
    registerBindings(scheme, kWidgetBindings);
    registerBindings(scheme, kThemedBindings);
}

void setPackageUiDrawSlot(PackageUiDrawSlot slot) {
    g_uiSlot = slot;
}

void callDrawFileMenu(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::FileMenu);
    callNamedHook(world, "draw-file-menu");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawMenuBar(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::MenuBar);
    callNamedHook(world, "draw-menu-bar");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawPauseMenu(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::PauseMenu);
    callNamedHook(world, "draw-pause-menu");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawDebugMenu(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::DebugMenu);
    callNamedHook(world, "draw-debug-menu");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawModals(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::Modals);
    callNamedHook(world, "draw-modals");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

}
