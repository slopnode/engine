#include "script/ui_script.hpp"
#include "script/script_scope.hpp"

#include "game/game_state.hpp"
#include "input/input_context.hpp"
#include "script/scheme_call.hpp"
#include "script/script_context.hpp"

#include "imgui.h"

#include <s7.h>

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
    return g_uiSlot == PackageUiDrawSlot::FileMenu || g_uiSlot == PackageUiDrawSlot::PauseMenu;
}

bool contextContains(InputContext context) {
    if (g_uiWorld == nullptr || !g_uiWorld->has<InputContextStack>()) {
        return false;
    }
    return g_uiWorld->get<InputContextStack>().contains(context);
}

s7_pointer g_playing_p(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    if (g_uiWorld == nullptr) {
        return s7_f(sc);
    }
    return isPlaying(*g_uiWorld) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_main_menu_p(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    return contextContains(InputContext::MainMenu) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_pause_menu_p(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::ReadWorld)) {
        return s7_f(sc);
    }
    return contextContains(InputContext::PauseMenu) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_text(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-text", 1, s7_car(args), "string");
    }
    ImGui::TextUnformatted(s7_string(s7_car(args)));
    return s7_t(sc);
}

s7_pointer g_ui_separator(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::Separator();
    return s7_t(sc);
}

s7_pointer g_ui_same_line(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::SameLine();
    return s7_t(sc);
}

s7_pointer g_ui_button(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-button", 1, s7_car(args), "string");
    }
    return ImGui::Button(s7_string(s7_car(args))) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_menu_item(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!inMenuItemSlot()) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-menu-item", 1, s7_car(args), "string");
    }
    bool enabled = true;
    if (s7_is_pair(s7_cdr(args))) {
        enabled = s7_boolean(sc, s7_cadr(args));
    }
    return ImGui::MenuItem(s7_string(s7_car(args)), nullptr, false, enabled) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_selectable(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-selectable", 1, s7_car(args), "string");
    }
    bool selected = false;
    if (s7_is_pair(s7_cdr(args))) {
        selected = s7_boolean(sc, s7_cadr(args));
    }
    return ImGui::Selectable(s7_string(s7_car(args)), selected) ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_begin(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-begin", 1, s7_car(args), "string");
    }
    const bool open = ImGui::Begin(
        s7_string(s7_car(args)),
        nullptr,
        ImGuiWindowFlags_NoCollapse);
    return open ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_end(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::End();
    return s7_t(sc);
}

s7_pointer g_ui_begin_child(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-begin-child", 1, s7_car(args), "string");
    }
    float height = 0.0f;
    if (s7_is_pair(s7_cdr(args)) && s7_is_number(s7_cadr(args))) {
        height = static_cast<float>(s7_number_to_real(sc, s7_cadr(args)));
    }
    const bool open = ImGui::BeginChild(
        s7_string(s7_car(args)),
        ImVec2{0.0f, height},
        ImGuiChildFlags_Borders);
    return open ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_end_child(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::EndChild();
    return s7_t(sc);
}

s7_pointer g_ui_set_next_window_size(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_number(s7_car(args)) || !s7_is_number(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "ui-set-next-window-size", 1, s7_car(args), "w h numbers");
    }
    ImGui::SetNextWindowSize(
        {static_cast<float>(s7_number_to_real(sc, s7_car(args))),
         static_cast<float>(s7_number_to_real(sc, s7_cadr(args)))},
        ImGuiCond_FirstUseEver);
    return s7_t(sc);
}

s7_pointer g_ui_center_next_window(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::SetNextWindowPos(
        {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        {0.5f, 0.5f});
    return s7_t(sc);
}

s7_pointer g_ui_input_text(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args)) || !s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "ui-input-text", 1, s7_car(args), "id initial-string");
    }
    const std::string id = s7_string(s7_car(args));
    const std::string initial = s7_string(s7_cadr(args));
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
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args)) || !s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "ui-input-text-set", 1, s7_car(args), "id string");
    }
    g_inputTextBuffers[s7_string(s7_car(args))] = s7_string(s7_cadr(args));
    return s7_t(sc);
}

s7_pointer g_ui_begin_combo(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args)) || !s7_is_string(s7_cadr(args))) {
        return s7_wrong_type_arg_error(sc, "ui-begin-combo", 1, s7_car(args), "id preview");
    }
    ImGui::PushID(s7_string(s7_car(args)));
    const bool open = ImGui::BeginCombo("##combo", s7_string(s7_cadr(args)));
    if (!open) {
        ImGui::PopID();
    }
    return open ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_combo_item(s7_scheme* sc, s7_pointer args) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    if (!s7_is_string(s7_car(args))) {
        return s7_wrong_type_arg_error(sc, "ui-combo-item", 1, s7_car(args), "string");
    }
    bool selected = false;
    if (s7_is_pair(s7_cdr(args))) {
        selected = s7_boolean(sc, s7_cadr(args));
    }
    const bool clicked = ImGui::Selectable(s7_string(s7_car(args)), selected);
    if (selected) {
        ImGui::SetItemDefaultFocus();
    }
    return clicked ? s7_t(sc) : s7_f(sc);
}

s7_pointer g_ui_end_combo(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::EndCombo();
    ImGui::PopID();
    return s7_t(sc);
}

s7_pointer g_ui_indent(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::Indent();
    return s7_t(sc);
}

s7_pointer g_ui_unindent(s7_scheme* sc, s7_pointer) {
    if (!requireCap(sc, ScriptCap::UiDraw)) {
        return s7_f(sc);
    }
    ImGui::Unindent();
    return s7_t(sc);
}

void callNamedHook(flecs::world& world, const char* name) {
    if (!world.has<ScriptContext>() || world.get<ScriptContext>().scheme == nullptr) {
        return;
    }
    tryCallSchemeProc(world.get<ScriptContext>().scheme, name, ScriptScope::Ui);
}

} // namespace

void bindUiApi(flecs::world& world, s7_scheme* scheme) {
    g_uiWorld = &world;
    if (scheme == nullptr) {
        return;
    }

    s7_define_function(scheme, "playing?", g_playing_p, 0, 0, false, "(playing?)");
    s7_define_function(scheme, "main-menu?", g_main_menu_p, 0, 0, false, "(main-menu?)");
    s7_define_function(scheme, "pause-menu?", g_pause_menu_p, 0, 0, false, "(pause-menu?)");
    s7_define_function(scheme, "ui-text", g_ui_text, 1, 0, false, "(ui-text str)");
    s7_define_function(scheme, "ui-separator", g_ui_separator, 0, 0, false, "(ui-separator)");
    s7_define_function(scheme, "ui-same-line", g_ui_same_line, 0, 0, false, "(ui-same-line)");
    s7_define_function(scheme, "ui-button", g_ui_button, 1, 0, false, "(ui-button label)");
    s7_define_function(
        scheme, "ui-menu-item", g_ui_menu_item, 1, 1, false, "(ui-menu-item label [enabled?])");
    s7_define_function(
        scheme, "ui-selectable", g_ui_selectable, 1, 1, false, "(ui-selectable label [selected?])");
    s7_define_function(scheme, "ui-begin", g_ui_begin, 1, 0, false, "(ui-begin title)");
    s7_define_function(scheme, "ui-end", g_ui_end, 0, 0, false, "(ui-end)");
    s7_define_function(
        scheme, "ui-begin-child", g_ui_begin_child, 1, 1, false, "(ui-begin-child id [height])");
    s7_define_function(scheme, "ui-end-child", g_ui_end_child, 0, 0, false, "(ui-end-child)");
    s7_define_function(
        scheme,
        "ui-set-next-window-size",
        g_ui_set_next_window_size,
        2,
        0,
        false,
        "(ui-set-next-window-size w h)");
    s7_define_function(
        scheme, "ui-center-next-window", g_ui_center_next_window, 0, 0, false,
        "(ui-center-next-window)");
    s7_define_function(
        scheme, "ui-input-text", g_ui_input_text, 2, 0, false, "(ui-input-text id initial)");
    s7_define_function(
        scheme, "ui-input-text-set", g_ui_input_text_set, 2, 0, false, "(ui-input-text-set id str)");
    s7_define_function(
        scheme, "ui-begin-combo", g_ui_begin_combo, 2, 0, false, "(ui-begin-combo id preview)");
    s7_define_function(
        scheme, "ui-combo-item", g_ui_combo_item, 1, 1, false, "(ui-combo-item label [selected?])");
    s7_define_function(scheme, "ui-end-combo", g_ui_end_combo, 0, 0, false, "(ui-end-combo)");
    s7_define_function(scheme, "ui-indent", g_ui_indent, 0, 0, false, "(ui-indent)");
    s7_define_function(scheme, "ui-unindent", g_ui_unindent, 0, 0, false, "(ui-unindent)");
}

void setPackageUiDrawSlot(PackageUiDrawSlot slot) {
    g_uiSlot = slot;
}

void callDrawFileMenu(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::FileMenu);
    callNamedHook(world, "draw-file-menu");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawPauseMenu(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::PauseMenu);
    callNamedHook(world, "draw-pause-menu");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

void callDrawModals(flecs::world& world) {
    setPackageUiDrawSlot(PackageUiDrawSlot::Modals);
    callNamedHook(world, "draw-modals");
    setPackageUiDrawSlot(PackageUiDrawSlot::None);
}

}
