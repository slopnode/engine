#include "browser.hpp"
#include "bus_editor.hpp"
#include "editor.hpp"
#include "filter_chain_ui.hpp"
#include "layout.hpp"
#include "waveform_view.hpp"

#include "assets/asset_store.hpp"
#include "audio/audio_world.hpp"
#include "core/log.hpp"
#include "core/package_meta.hpp"
#include "core/package_search.hpp"
#include "core/user_paths.hpp"
#include "game/app_config.hpp"
#include "ui/icon_ui.hpp"
#include "ui/imgui_fonts.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>

using slopengine::beginMainMenuBar;
using slopengine::beginMenuWithIcon;
using slopengine::buttonWithIcon;
using slopengine::endMainMenuBar;
using slopengine::kDefaultIconSet;
using slopengine::menuItemWithIcon;

namespace {

struct ToolConfig {
    slopengine::AppConfig mount;
    std::filesystem::path target;
};

void printUsage() {
    std::cerr
        << "Usage: slopaudio --game <path> [--mod <path>]... [--target <path>]\n"
        << "\n"
        << "  --game        Game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives .saudio saves\n"
        << "                (defaults to --game; required when --mod is given)\n"
        << "  --verbose     Raise the trace log level to show DEBUG messages\n";
}

std::optional<ToolConfig> parseArgs(int argc, char* argv[]) {
    ToolConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char*) -> const char* {
            if (i + 1 >= argc) {
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--game") {
            const char* value = needValue("--game");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.game = value;
            continue;
        }
        if (arg == "--mod") {
            const char* value = needValue("--mod");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.mods.emplace_back(value);
            continue;
        }
        if (arg == "--target") {
            const char* value = needValue("--target");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.target = slopengine::resolveApplicationPackagePath(
                value,
                slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
            continue;
        }
        if (arg == "--verbose") {
            config.mount.verbose = true;
            continue;
        }
        return std::nullopt;
    }

    if (config.mount.game.empty()) {
        return std::nullopt;
    }
    if (config.target.empty()) {
        if (!config.mount.mods.empty()) {
            return std::nullopt;
        }
        config.target = slopengine::resolveApplicationPackagePath(
            config.mount.game,
            slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
    }
    return config;
}

bool pathEquals(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    const auto ca = std::filesystem::weakly_canonical(a, ec);
    if (ec) {
        return a == b;
    }
    const auto cb = std::filesystem::weakly_canonical(b, ec);
    if (ec) {
        return a == b;
    }
    return ca == cb;
}

bool targetIsMounted(const slopengine::AssetStore& assets, const std::filesystem::path& target) {
    for (const slopengine::Package& package : assets.packages()) {
        if (pathEquals(package.root(), target)) {
            return true;
        }
    }
    return false;
}

const char* presetNames[] = {"coin", "laser", "explosion", "powerup", "hurt", "jump", "blip"};

void sliderField(
    slopaudio::Editor& editor,
    const char* label,
    float& value,
    std::uint32_t field,
    float lo,
    float hi) {
    if (ImGui::SliderFloat(label, &value, lo, hi)) {
        editor.doc.procedural.sfxr.setMask |= field;
        editor.markDirty();
    }
}

// Shows/edits a raw 0..1 sfxr parameter through a real-world unit (Hz, ms, %), using the same
// formulas SoLoud::Sfxr itself uses internally (soloud_sfxr.cpp: resetSample()) to convert.
void sliderConverted(
    slopaudio::Editor& editor,
    const char* label,
    float& raw,
    std::uint32_t field,
    float dispLo,
    float dispHi,
    const char* format,
    float (*toDisplay)(float),
    float (*toRaw)(float)) {
    float display = toDisplay(raw);
    if (ImGui::SliderFloat(label, &display, dispLo, dispHi, format)) {
        raw = std::clamp(toRaw(display), 0.0f, 1.0f);
        editor.doc.procedural.sfxr.setMask |= field;
        editor.markDirty();
    }
}

// sfxr's frequency-ish parameters are all 0..1 dials mapped through the period the wave engine
// actually steps at (soloud_sfxr.cpp:250 `fperiod = 100/(t*t+0.001)`, output at 44100Hz), or
// through a similar low-pass/high-pass cutoff coefficient (soloud_sfxr.cpp:270,275). Exposing
// them in Hz/ms/% instead of the bare 0..1 dial is what makes these sliders legible.
float sfxrBaseFreqToHz(float t) { return 441.0f * (t * t + 0.001f); }
float sfxrHzToBaseFreq(float hz) { return std::sqrt(std::max(0.0f, hz / 441.0f - 0.001f)); }

float sfxrEnvToMs(float t) { return t * t * 2267.57f; }
float sfxrMsToEnv(float ms) { return std::sqrt(std::max(0.0f, ms) / 2267.57f); }

float sfxrVibToHz(float t) { return t * t * 70.17f; }
float sfxrHzToVib(float hz) { return std::sqrt(std::max(0.0f, hz) / 70.17f); }

// LPF/HPF cutoffs approximate a one-pole filter coefficient as fc ~= coeff * sampleRate / 2pi.
float sfxrLpfToHz(float t) { return t * t * t * 701.87f; }
float sfxrHzToLpf(float hz) { return std::cbrt(std::max(0.0f, hz) / 701.87f); }
float sfxrHpfToHz(float t) { return t * t * 701.87f; }
float sfxrHzToHpf(float hz) { return std::sqrt(std::max(0.0f, hz) / 701.87f); }

float sfxrDutyToPulseWidthPct(float t) { return (0.5f - t * 0.5f) * 100.0f; }
float sfxrPulseWidthPctToDuty(float pct) { return 1.0f - pct / 50.0f; }

float sfxrToPercent(float t) { return t * 100.0f; }
float sfxrFromPercent(float pct) { return pct / 100.0f; }

void drawProceduralInspector(slopaudio::Editor& editor, slopengine::AssetStore& assets) {
    slopengine::SaudioParams& p = editor.doc.procedural.sfxr;

    ImGui::SeparatorText("Wave");
    {
        int waveIdx = p.waveType;
        if (ImGui::Combo("Type", &waveIdx, "square\0saw\0sine\0noise\0")) {
            p.waveType = waveIdx;
            p.setMask |= slopengine::SaudioField_Wave;
            editor.markDirty();
        }

        static int presetIdx = 6;
        static int presetSeed = 0;
        ImGui::Combo("Preset", &presetIdx, presetNames, IM_ARRAYSIZE(presetNames));
        ImGui::DragInt("Seed", &presetSeed, 1.0f, 0, 999999);
        if (buttonWithIcon(assets, kDefaultIconSet, "wand", "Apply Preset")) {
            p = slopaudio::materializePreset(presetIdx, presetSeed);
            editor.markDirty();
        }
    }

    ImGui::SeparatorText("Frequency");
    sliderConverted(
        editor, "Base Freq", p.baseFreq, slopengine::SaudioField_Freq, 0.0f, 441.0f, "%.1f Hz",
        sfxrBaseFreqToHz, sfxrHzToBaseFreq);
    sliderConverted(
        editor, "Freq Limit", p.freqLimit, slopengine::SaudioField_FreqLimit, 0.0f, 441.0f, "%.1f Hz",
        sfxrBaseFreqToHz, sfxrHzToBaseFreq);
    sliderField(editor, "Freq Ramp", p.freqRamp, slopengine::SaudioField_FreqRamp, -1.0f, 1.0f);
    sliderField(editor, "Freq Delta Ramp", p.freqDramp, slopengine::SaudioField_FreqDramp, -1.0f, 1.0f);
    sliderConverted(
        editor, "Pulse Width", p.duty, slopengine::SaudioField_Duty, 0.0f, 50.0f, "%.0f%%",
        sfxrDutyToPulseWidthPct, sfxrPulseWidthPctToDuty);
    sliderField(editor, "Duty Ramp", p.dutyRamp, slopengine::SaudioField_DutyRamp, -1.0f, 1.0f);

    ImGui::SeparatorText("Vibrato");
    sliderConverted(
        editor, "Strength", p.vibStrength, slopengine::SaudioField_VibStrength, 0.0f, 100.0f, "%.0f%%",
        sfxrToPercent, sfxrFromPercent);
    sliderConverted(
        editor, "Speed", p.vibSpeed, slopengine::SaudioField_VibSpeed, 0.0f, 70.0f, "%.1f Hz",
        sfxrVibToHz, sfxrHzToVib);
    sliderField(editor, "Delay", p.vibDelay, slopengine::SaudioField_VibDelay, 0.0f, 1.0f);

    ImGui::SeparatorText("Envelope");
    sliderConverted(
        editor, "Attack", p.envAttack, slopengine::SaudioField_Attack, 0.0f, 1000.0f, "%.1f ms",
        sfxrEnvToMs, sfxrMsToEnv);
    sliderConverted(
        editor, "Sustain", p.envSustain, slopengine::SaudioField_Sustain, 0.0f, 1000.0f, "%.1f ms",
        sfxrEnvToMs, sfxrMsToEnv);
    sliderConverted(
        editor, "Decay", p.envDecay, slopengine::SaudioField_Decay, 0.0f, 1000.0f, "%.1f ms",
        sfxrEnvToMs, sfxrMsToEnv);
    sliderConverted(
        editor, "Punch", p.envPunch, slopengine::SaudioField_Punch, 0.0f, 100.0f, "%.0f%%",
        sfxrToPercent, sfxrFromPercent);

    ImGui::SeparatorText("Filters (sfxr)");
    sliderConverted(
        editor, "LPF Cutoff", p.lpfFreq, slopengine::SaudioField_Lpf, 0.0f, 700.0f, "~%.0f Hz",
        sfxrLpfToHz, sfxrHzToLpf);
    sliderConverted(
        editor, "LPF Resonance", p.lpfResonance, slopengine::SaudioField_LpfResonance, 0.0f, 100.0f, "%.0f%%",
        sfxrToPercent, sfxrFromPercent);
    sliderField(editor, "LPF Ramp", p.lpfRamp, slopengine::SaudioField_LpfRamp, -1.0f, 1.0f);
    sliderConverted(
        editor, "HPF Cutoff", p.hpfFreq, slopengine::SaudioField_Hpf, 0.0f, 700.0f, "~%.0f Hz",
        sfxrHpfToHz, sfxrHzToHpf);
    sliderField(editor, "HPF Ramp", p.hpfRamp, slopengine::SaudioField_HpfRamp, -1.0f, 1.0f);
    sliderField(editor, "Phaser Offset", p.phaOffset, slopengine::SaudioField_PhaOffset, -1.0f, 1.0f);
    sliderField(editor, "Phaser Ramp", p.phaRamp, slopengine::SaudioField_PhaRamp, -1.0f, 1.0f);

    ImGui::SeparatorText("Repeat / Arp");
    sliderField(editor, "Repeat Speed", p.repeatSpeed, slopengine::SaudioField_RepeatSpeed, 0.0f, 1.0f);
    sliderField(editor, "Arp Speed", p.arpSpeed, slopengine::SaudioField_ArpSpeed, 0.0f, 1.0f);
    sliderField(editor, "Arp Mod", p.arpMod, slopengine::SaudioField_ArpMod, -1.0f, 1.0f);

    ImGui::SeparatorText("Output");
    sliderConverted(
        editor, "Sound Vol", p.soundVol, slopengine::SaudioField_SoundVol, 0.0f, 100.0f, "%.0f%%",
        sfxrToPercent, sfxrFromPercent);
    sliderConverted(
        editor, "Master Vol", p.masterVol, slopengine::SaudioField_MasterVol, 0.0f, 100.0f, "%.0f%%",
        sfxrToPercent, sfxrFromPercent);
}

void drawDefinitionInspector(
    slopaudio::Editor& editor,
    slopengine::AssetStore& assets,
    const slopaudio::Browser& browser,
    slopengine::AudioWorld& audioWorld) {
    slopengine::AudioDef& def = editor.doc.asset;

    ImGui::SeparatorText("Source");
    char sourceBuf[256] = {};
    std::snprintf(sourceBuf, sizeof(sourceBuf), "%s", def.source.c_str());
    ImGui::SetNextItemWidth(-70.0f);
    if (ImGui::InputText("##defsource", sourceBuf, sizeof(sourceBuf))) {
        def.source = sourceBuf;
        editor.markDirty();
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kDefaultIconSet, "folder_find", "Browse")) {
        ImGui::OpenPopup("PickDefinitionSource");
    }
    if (ImGui::BeginPopup("PickDefinitionSource")) {
        ImGui::TextDisabled("Files (sound/*.ogg,*.wav)");
        ImGui::Separator();
        for (const slopaudio::AudioBrowserEntry& entry : browser.entries) {
            if (entry.kind != slopaudio::SourceKind::Clip) {
                continue;
            }
            if (ImGui::Selectable(entry.virtualPath.c_str())) {
                def.source = entry.virtualPath;
                editor.markDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Procedural (audio/*.sfxr)");
        ImGui::Separator();
        for (const slopaudio::AudioBrowserEntry& entry : browser.entries) {
            if (entry.kind != slopaudio::SourceKind::Procedural) {
                continue;
            }
            if (ImGui::Selectable(entry.virtualPath.c_str())) {
                def.source = entry.virtualPath;
                editor.markDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Playback");
    if (ImGui::SliderFloat("Volume", &def.volume, 0.0f, 2.0f)) {
        editor.markDirty();
    }
    if (ImGui::Checkbox("Loop", &def.loop)) {
        editor.markDirty();
    }
    if (ImGui::Checkbox("Spatial", &def.spatial)) {
        editor.markDirty();
    }
    if (ImGui::SliderFloat("Min Distance", &def.minDistance, 0.0f, 100.0f)) {
        editor.markDirty();
    }
    if (ImGui::SliderFloat("Max Distance", &def.maxDistance, 0.0f, 200.0f)) {
        editor.markDirty();
    }
    int busIdx = static_cast<int>(def.bus);
    if (ImGui::Combo("Bus", &busIdx, "sfx\0music\0ui\0hud\0voices\0ambience\0player\0view\0")) {
        def.bus = static_cast<slopengine::AudioDefBus>(busIdx);
        editor.markDirty();
    }

    ImGui::SeparatorText("Filters");
    ImGui::TextDisabled("Applied to this definition only, before the bus mix.");

    const bool structureChanged = slopaudio::drawFilterChain(
        def.filters,
        [&](unsigned int slot, unsigned int paramIndex, float value) {
            editor.markDirty();
            if (editor.doc.previewPlaying) {
                audioWorld.soloud().setFilterParameter(editor.doc.previewVoice, slot, paramIndex, value);
            }
        });
    if (structureChanged) {
        audioWorld.invalidateDefCache(editor.doc.virtualPath);
        editor.markDirty();
    }
}

void drawInspector(
    slopaudio::Editor& editor,
    slopengine::AssetStore& assets,
    const slopaudio::Browser& browser,
    slopengine::AudioWorld& audioWorld) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("No sound loaded");
        return;
    }

    ImGui::TextUnformatted(editor.doc.virtualPath.c_str());
    if (editor.doc.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "*");
    }
    ImGui::Separator();

    switch (editor.doc.sourceKind) {
        case slopaudio::SourceKind::Clip:
            ImGui::TextDisabled("Preview-only clip (sound/*.ogg)");
            ImGui::SliderFloat("Volume", &editor.doc.asset.volume, 0.0f, 2.0f);
            break;
        case slopaudio::SourceKind::Definition:
            drawDefinitionInspector(editor, assets, browser, audioWorld);
            break;
        case slopaudio::SourceKind::Procedural:
            drawProceduralInspector(editor, assets);
            break;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const auto config = parseArgs(argc, argv);
    if (!config) {
        printUsage();
        return 1;
    }

    slopengine::Log::init(config->mount.verbose ? slopengine::LogLevel::Debug : slopengine::LogLevel::Info);
    slopengine::Log::addDefaultConsoleSink();
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1400, 820, "slopaudio");
    if (!IsWindowReady()) {
        std::cerr << "slopaudio: failed to initialize window\n";
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopaudio: --target must be one of the mounted packages\n";
        assets.releaseGpuResources();
        CloseWindow();
        return 1;
    }

    slopengine::setupImGuiWithUiFont(assets, slopengine::kDefaultUiFontPath, true, "slopaudio");

    slopaudio::Editor editor;
    editor.targetRoot = config->target;

    slopaudio::Browser browser;
    browser.rescan(assets);

    slopaudio::BusEditor busEditor;
    busEditor.targetRoot = config->target;

    slopengine::AudioWorld audioWorld;
    audioWorld.init();

    enum class TopTab { Sources, Bus };
    TopTab activeTab = TopTab::Sources;

    bool quit = false;
    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }

        if (editor.doc.waveformDirty && editor.doc.open) {
            editor.rebuildWaveform(assets);
        }
        if (editor.doc.previewPlaying && !audioWorld.soloud().isValidVoiceHandle(editor.doc.previewVoice)) {
            editor.doc.previewPlaying = false;
            editor.doc.previewVoice = 0;
        }
        if (editor.statusTimer > 0.0f) {
            editor.statusTimer -= GetFrameTime();
            if (editor.statusTimer <= 0.0f) {
                editor.statusMessage.clear();
            }
        }

        rlImGuiBegin();

        const float chromeHeight = slopengine::mainMenuBarHeight();
        const float tabBarHeight = ImGui::GetFrameHeightWithSpacing();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const slopaudio::UiLayout layout =
            slopaudio::computeUiLayout(chromeHeight, tabBarHeight, statusHeight);

        if (beginMainMenuBar()) {
            if (beginMenuWithIcon(assets, kDefaultIconSet, "folder", "File", true)) {
                if (menuItemWithIcon(assets, kDefaultIconSet, "page_add", "New Definition...", "Ctrl+N")) {
                    editor.newModalKind = slopaudio::SourceKind::Definition;
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "wand", "New Procedural...")) {
                    editor.newModalKind = slopaudio::SourceKind::Procedural;
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kDefaultIconSet,
                        "disk",
                        "Save",
                        "Ctrl+S",
                        false,
                        editor.doc.open && editor.doc.sourceKind != slopaudio::SourceKind::Clip)) {
                    editor.save(assets);
                    browser.rescan(assets);
                }
                if (menuItemWithIcon(assets, kDefaultIconSet, "arrow_refresh", "Rescan")) {
                    browser.rescan(assets);
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kDefaultIconSet, "door", "Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            endMainMenuBar();
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S) && editor.doc.open &&
            editor.doc.sourceKind != slopaudio::SourceKind::Clip) {
            editor.save(assets);
            browser.rescan(assets);
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N)) {
            editor.newModalKind = browser.activeTab == slopaudio::SourceKind::Procedural
                ? slopaudio::SourceKind::Procedural
                : slopaudio::SourceKind::Definition;
            editor.showNewModal = true;
        }

        if (editor.showNewModal) {
            ImGui::OpenPopup("New Sound");
            editor.showNewModal = false;
        }
        if (ImGui::BeginPopupModal("New Sound", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool procedural = editor.newModalKind == slopaudio::SourceKind::Procedural;
            ImGui::Text(
                "New %s -- virtual path under audio/ (no extension)", procedural ? "Procedural" : "Definition");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint(
                "##newaudiopath", "e.g. weapons/pistol-fire", editor.newPathBuf, sizeof(editor.newPathBuf));
            if (editor.doc.dirty) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Current sound has unsaved changes.");
            }
            const char* createLabel = editor.doc.dirty ? "Discard & Create" : "Create";
            if (buttonWithIcon(assets, kDefaultIconSet, "page_add", createLabel, ImVec2(160, 0))) {
                const bool created = procedural ? editor.newProcedural(editor.newPathBuf)
                                                 : editor.newDefinition(editor.newPathBuf);
                if (created) {
                    editor.newPathBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kDefaultIconSet, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        const ImGuiWindowFlags scrollablePanelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos(ImVec2(layout.tabBar.x, layout.tabBar.y));
        ImGui::SetNextWindowSize(ImVec2(layout.tabBar.width, layout.tabBar.height));
        ImGui::Begin(
            "##maintabs",
            nullptr,
            panelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Sources")) {
                activeTab = TopTab::Sources;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bus")) {
                activeTab = TopTab::Bus;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        if (activeTab == TopTab::Sources) {
            ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
            ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
            ImGui::Begin("Browser", nullptr, panelFlags);
            browser.draw(editor, assets, audioWorld);
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
            ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
            ImGui::Begin("Inspector", nullptr, scrollablePanelFlags);
            drawInspector(editor, assets, browser, audioWorld);
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(layout.content.x, layout.content.y));
            ImGui::SetNextWindowSize(ImVec2(layout.content.width, layout.content.height));
            ImGui::Begin(
                "Waveform",
                nullptr,
                panelFlags | ImGuiWindowFlags_NoTitleBar);
            {
                const bool canPlay = editor.doc.open;
                const bool hasFilters = editor.doc.sourceKind == slopaudio::SourceKind::Definition &&
                    slopengine::hasAnyFilterSlots(editor.doc.asset.filters);
                ImGui::BeginDisabled(!canPlay);
                if (buttonWithIcon(assets, kDefaultIconSet, "control_play", "Play Source", ImVec2(100, 0))) {
                    editor.previewPlaySource(audioWorld, assets);
                }
                ImGui::SameLine();
                if (hasFilters) {
                    if (buttonWithIcon(assets, kDefaultIconSet, "control_play", "Play Filtered", ImVec2(100, 0))) {
                        editor.previewPlayFiltered(audioWorld, assets);
                    }
                    ImGui::SameLine();
                }
                if (buttonWithIcon(assets, kDefaultIconSet, "control_play", "Play Bus", ImVec2(100, 0))) {
                    editor.previewPlayBus(audioWorld, assets);
                }
                ImGui::SameLine();
                if (buttonWithIcon(assets, kDefaultIconSet, "control_stop", "Stop", ImVec2(100, 0))) {
                    editor.previewStop(audioWorld);
                }
                ImGui::EndDisabled();
                if (editor.doc.sourceKind == slopaudio::SourceKind::Procedural &&
                    editor.doc.procedural.sfxr.hasPreset) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "%s (%s, seed %d)",
                        editor.doc.virtualPath.c_str(),
                        presetNames[editor.doc.procedural.sfxr.preset % IM_ARRAYSIZE(presetNames)],
                        editor.doc.procedural.sfxr.presetSeed);
                }

                float playhead = -1.0f;
                if (editor.doc.previewPlaying && !editor.doc.waveform.samples.empty() &&
                    editor.doc.waveform.sampleRate > 0.0f) {
                    const double posSeconds = audioWorld.soloud().getStreamPosition(editor.doc.previewVoice);
                    const unsigned int waveformChannels = std::max(1u, editor.doc.waveform.channels);
                    const double totalSeconds =
                        static_cast<double>(editor.doc.waveform.samples.size() / waveformChannels) /
                        static_cast<double>(editor.doc.waveform.sampleRate);
                    if (totalSeconds > 0.0) {
                        playhead = static_cast<float>(posSeconds / totalSeconds);
                    }
                }
                slopaudio::drawWaveformPanel(
                    editor.doc.waveform,
                    editor.doc.filteredWaveform,
                    editor.doc.showSampleWaveform,
                    editor.doc.showFilteredWaveform,
                    editor.doc.splitChannelsWaveform,
                    editor.waveformWindowMs,
                    playhead,
                    ImGui::GetContentRegionAvail());
            }
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
            ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
            ImGui::Begin("Buses", nullptr, panelFlags);
            busEditor.drawBusList(assets, audioWorld);
            ImGui::End();

            const Rectangle busSettingsRect{
                layout.leftPanel.x + layout.leftPanel.width,
                layout.leftPanel.y,
                layout.fullPanel.width - layout.leftPanel.width,
                layout.leftPanel.height,
            };
            ImGui::SetNextWindowPos(ImVec2(busSettingsRect.x, busSettingsRect.y));
            ImGui::SetNextWindowSize(ImVec2(busSettingsRect.width, busSettingsRect.height));
            ImGui::Begin("Bus Settings", nullptr, scrollablePanelFlags);
            busEditor.drawBusSettings(assets, audioWorld);
            ImGui::End();
        }

        std::string statusLine = "slopaudio";
        if (editor.doc.open) {
            statusLine += " | ";
            statusLine += editor.doc.virtualPath;
            if (editor.doc.dirty) {
                statusLine += "*";
            }
        }
        if (!editor.statusMessage.empty()) {
            statusLine += " | ";
            statusLine += editor.statusMessage;
        }

        ImGui::SetNextWindowPos(
            ImVec2(0.0f, static_cast<float>(GetScreenHeight()) - statusHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(GetScreenWidth()), statusHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::Begin(
                "##status",
                nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            ImGui::TextUnformatted(statusLine.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar();

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        rlImGuiEnd();
        EndDrawing();
    }

    audioWorld.deinit();
    assets.releaseGpuResources();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
