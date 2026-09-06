#include "browser.hpp"
#include "editor.hpp"
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
        << "Usage: slopaudio --base-game <path> [--mod <path>]... --target <path>\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives .saudio saves (required)\n"
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

        if (arg == "--base-game") {
            const char* value = needValue("--base-game");
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.base_game = value;
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

    if (config.mount.base_game.empty() || config.target.empty()) {
        return std::nullopt;
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
        editor.doc.asset.sfxr.setMask |= field;
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
        editor.doc.asset.sfxr.setMask |= field;
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

void drawSaudioInspector(slopaudio::Editor& editor, slopengine::AssetStore& assets) {
    slopengine::SaudioParams& p = editor.doc.asset.sfxr;
    slopengine::AudioDef& def = editor.doc.asset;

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
            def.sfxr = slopaudio::materializePreset(presetIdx, presetSeed);
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
    ImGui::Separator();
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
    int busIdx = def.bus == slopengine::AudioDefBus::Music ? 1 : 0;
    if (ImGui::Combo("Bus", &busIdx, "sfx\0music\0")) {
        def.bus = busIdx == 1 ? slopengine::AudioDefBus::Music : slopengine::AudioDefBus::Sfx;
        editor.markDirty();
    }
}

void drawInspector(slopaudio::Editor& editor, slopengine::AssetStore& assets) {
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

    if (editor.doc.sourceKind == slopaudio::SourceKind::Clip) {
        ImGui::TextDisabled("Preview-only clip (sound/*.ogg|.wav)");
        ImGui::SliderFloat("Volume", &editor.doc.asset.volume, 0.0f, 2.0f);
        return;
    }

    drawSaudioInspector(editor, assets);
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
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
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

    slopengine::AudioWorld audioWorld;
    audioWorld.init();

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
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float transportHeight = 96.0f;
        const slopaudio::UiLayout layout =
            slopaudio::computeUiLayout(chromeHeight, statusHeight, transportHeight);

        if (beginMainMenuBar()) {
            if (beginMenuWithIcon(assets, kDefaultIconSet, "folder", "File", true)) {
                if (menuItemWithIcon(assets, kDefaultIconSet, "page_add", "New Sound...", "Ctrl+N")) {
                    editor.showNewModal = true;
                }
                if (menuItemWithIcon(
                        assets,
                        kDefaultIconSet,
                        "disk",
                        "Save",
                        "Ctrl+S",
                        false,
                        editor.doc.open && editor.doc.sourceKind == slopaudio::SourceKind::Saudio)) {
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
            editor.doc.sourceKind == slopaudio::SourceKind::Saudio) {
            editor.save(assets);
            browser.rescan(assets);
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N)) {
            editor.showNewModal = true;
        }

        if (editor.showNewModal) {
            ImGui::OpenPopup("New Sound");
            editor.showNewModal = false;
        }
        if (ImGui::BeginPopupModal("New Sound", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Virtual path under audio/ (no .saudio)");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint(
                "##newaudiopath", "e.g. weapons/pistol-fire", editor.newPathBuf, sizeof(editor.newPathBuf));
            if (editor.doc.dirty) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Current sound has unsaved changes.");
            }
            const char* createLabel = editor.doc.dirty ? "Discard & Create" : "Create";
            if (buttonWithIcon(assets, kDefaultIconSet, "page_add", createLabel, ImVec2(160, 0))) {
                if (editor.newAudio(editor.newPathBuf)) {
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

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        ImGui::Begin("Browser", nullptr, panelFlags);
        browser.draw(editor, assets, audioWorld);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        ImGui::Begin("Inspector", nullptr, scrollablePanelFlags);
        drawInspector(editor, assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.content.x, layout.content.y));
        ImGui::SetNextWindowSize(ImVec2(layout.content.width, layout.content.height));
        ImGui::Begin(
            "Waveform",
            nullptr,
            panelFlags | ImGuiWindowFlags_NoTitleBar);
        {
            float playhead = -1.0f;
            if (editor.doc.previewPlaying && !editor.doc.waveform.samples.empty() &&
                editor.doc.waveform.sampleRate > 0.0f) {
                const double posSeconds = audioWorld.soloud().getStreamPosition(editor.doc.previewVoice);
                const double totalSeconds = static_cast<double>(editor.doc.waveform.samples.size()) /
                    static_cast<double>(editor.doc.waveform.sampleRate);
                if (totalSeconds > 0.0) {
                    playhead = static_cast<float>(posSeconds / totalSeconds);
                }
            }
            slopaudio::drawWaveformPanel(
                editor.doc.waveform, editor.waveformWindowMs, playhead, ImGui::GetContentRegionAvail());
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.transportPanel.x, layout.transportPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.transportPanel.width, layout.transportPanel.height));
        ImGui::Begin(
            "Transport",
            nullptr,
            panelFlags | ImGuiWindowFlags_NoTitleBar);
        {
            const bool canPlay = editor.doc.open;
            ImGui::BeginDisabled(!canPlay);
            if (buttonWithIcon(assets, kDefaultIconSet, "control_play", "Play", ImVec2(100, 0))) {
                editor.previewPlay(audioWorld, assets);
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kDefaultIconSet, "control_stop", "Stop", ImVec2(100, 0))) {
                editor.previewStop(audioWorld);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (editor.doc.sourceKind == slopaudio::SourceKind::Saudio && editor.doc.asset.sfxr.hasPreset) {
                ImGui::TextDisabled(
                    "%s (%s, seed %d)",
                    editor.doc.virtualPath.c_str(),
                    presetNames[editor.doc.asset.sfxr.preset % IM_ARRAYSIZE(presetNames)],
                    editor.doc.asset.sfxr.presetSeed);
            }
        }
        ImGui::End();

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
