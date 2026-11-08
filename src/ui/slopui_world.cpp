#include "ui/slopui_world.hpp"

#include "assets/asset_store.hpp"
#include "audio/audio_module.hpp"
#include "ui/slopui_theme.hpp"

#include <imgui_internal.h>
#include <raylib.h>

#include <string>
#include <unordered_set>

namespace slopengine {

void installSlopUiTheme(flecs::world& world, AssetStore& assets, s7_scheme* scheme, std::string_view themeId) {
    world.component<SlopUiTheme>();
    world.component<SlopUiFonts>();
    world.component<SlopUiThemeReload>();
    world.component<SlopUiThemeCatalog>();
    world.set<SlopUiThemeCatalog>({listSlopUiThemes(assets, scheme)});
    SlopUiTheme theme = loadSlopUiTheme(assets, scheme, false, themeId);
    SlopUiFonts fonts{};
    if (ImGui::GetCurrentContext() != nullptr) {
        applySlopUiTheme(theme, assets, fonts);
    }
    world.set<SlopUiTheme>(std::move(theme));
    world.set<SlopUiFonts>(fonts);
    if (!world.has<SlopUiThemeReload>()) {
        world.set<SlopUiThemeReload>({});
    }
    setActiveSlopUi(&world.get<SlopUiTheme>(), &world.get<SlopUiFonts>(), &assets);
}

void playUiSound(flecs::world world, UiSoundKind kind) {
    if (!world.has<SlopUiTheme>() || !world.has<AudioContext>()) {
        return;
    }
    const SlopUiTheme& theme = world.get<SlopUiTheme>();
    const std::string* path = nullptr;
    switch (kind) {
    case UiSoundKind::Hover:
        path = &theme.sounds.hover;
        break;
    case UiSoundKind::Confirm:
        path = &theme.sounds.confirm;
        break;
    case UiSoundKind::Back:
        path = &theme.sounds.back;
        break;
    }
    if (path == nullptr || path->empty()) {
        return;
    }
    const AudioContext& ctx = world.get<AudioContext>();
    if (ctx.world == nullptr || ctx.assets == nullptr || !ctx.world->ready()) {
        return;
    }
    const AudioDef* def = ctx.assets->getAudioDef(*path);
    if (def == nullptr) {
        static std::unordered_set<std::string> warned;
        if (warned.insert(*path).second) {
            TraceLog(LOG_WARNING, "THEME: ui sound def not found: %s", path->c_str());
        }
        return;
    }
    ctx.world->playAudioDef(*ctx.assets, *path, *def, theme.sounds.volume);
}

void updateUiHoverSound(flecs::world world) {
    if (!world.has<SlopUiSoundState>()) {
        world.set<SlopUiSoundState>({});
    }
    SlopUiSoundState& state = world.get_mut<SlopUiSoundState>();
    const ImGuiID hovered = ImGui::GetHoveredID();
    if (hovered == state.lastHovered) {
        return;
    }
    state.lastHovered = hovered;
    if (hovered != 0) {
        playUiSound(world, UiSoundKind::Hover);
    }
}

}
