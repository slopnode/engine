#include "align_preview.hpp"
#include "browser.hpp"
#include "camera.hpp"
#include "editor.hpp"
#include "fp_preview.hpp"
#include "layout.hpp"
#include "sound_browser.hpp"
#include "texture_browser.hpp"
#include "world_preview.hpp"

#include "assets/asset_store.hpp"
#include "audio/audio_world.hpp"
#include "core/package_meta.hpp"
#include "game/app_config.hpp"
#include "ui/icon_ui.hpp"
#include "ui/imgui_fonts.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using slopengine::collapsingHeaderWithIcon;
using slopengine::kDefaultIconSet;

namespace {

struct ToolConfig {
    slopengine::AppConfig mount;
    std::filesystem::path target;
};

void printUsage() {
    std::cerr
        << "Usage: slopsprite --base-game <path> [--mod <path>]... --target <path>\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives sprite saves (required)\n";
}

std::optional<ToolConfig> parseArgs(int argc, char* argv[]) {
    ToolConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* flag) -> const char* {
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
            config.target = value;
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

void drawAnimBar(slopsprite::Editor& editor) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("No sprite open");
        return;
    }
    if (!editor.doc.hasAnim || editor.doc.animBank.clips.empty()) {
        ImGui::TextDisabled("No .spanim — create one in the Clip frames panel");
        return;
    }

    ImGui::TextUnformatted("Clip");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* preview =
        editor.doc.animClip.empty() ? "(none)" : editor.doc.animClip.c_str();
    if (ImGui::BeginCombo("##animclip", preview)) {
        for (const slopengine::SpriteAnimClip& clip : editor.doc.animBank.clips) {
            const bool selected = clip.name == editor.doc.animClip;
            if (ImGui::Selectable(clip.name.c_str(), selected)) {
                editor.doc.animLoop = clip.loop;
                editor.playAnimClip(clip.name, editor.doc.animLoop);
                editor.doc.animPlaying = false;
                editor.scrubAnim(0.0f);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button(editor.doc.animPlaying ? "Pause" : "Play")) {
        if (editor.doc.animPlaying) {
            editor.stopAnim();
        } else {
            editor.playAnimClip(editor.doc.animClip, editor.doc.animLoop);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        editor.stopAnim();
        editor.scrubAnim(0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Loop", &editor.doc.animLoop)) {
        if (slopengine::SpriteAnimClip* clip = editor.currentAnimClip()) {
            clip->loop = editor.doc.animLoop;
            editor.doc.animDirty = true;
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("Speed", &editor.doc.animSpeed, 0.01f, 0.01f, 8.0f, "%.2f");

    float timeMs = editor.doc.animTime * 1000.0f;
    const float durationMs = std::max(editor.doc.animDuration * 1000.0f, 1.0f);
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::SliderFloat("##animscrub", &timeMs, 0.0f, durationMs, "%.0fms")) {
        editor.doc.animPlaying = false;
        editor.scrubAnim(timeMs / 1000.0f);
    }
    ImGui::SameLine();
    ImGui::Text("%s%s", editor.doc.currentFrame.c_str(), editor.doc.animDirty ? "*" : "");
}

void drawClipFramesSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::SoundBrowser& soundBrowser) {
    if (!editor.doc.hasAnim || editor.doc.animBank.clips.empty()) {
        ImGui::TextDisabled("No .spanim for this sprite");
        if (ImGui::Button("Create .spanim", ImVec2(-1.0f, 0.0f))) {
            editor.ensureAnimBank();
        }
        return;
    }

    slopengine::SpriteAnimClip* clip = editor.currentAnimClip();
    if (clip == nullptr) {
        ImGui::TextDisabled("Select a clip in the timeline");
        return;
    }

    if (ImGui::Button("Append selected .spr frame", ImVec2(-1.0f, 0.0f))) {
        if (editor.doc.selectedFrameIndex >= 0 &&
            editor.doc.selectedFrameIndex < static_cast<int>(editor.doc.asset.frames.size())) {
            slopengine::SpriteAnimFrame animFrame{};
            animFrame.id =
                editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)].id;
            animFrame.duration = 0.1f;
            clip->frames.push_back(std::move(animFrame));
            editor.doc.animDirty = true;
            editor.doc.animDuration = editor.clipDuration(editor.doc.animClip);
        }
    }
    ImGui::TextDisabled("Tween out: R rot, S scale, T translate");

    static int soundPickFrameIndex = -1;

    for (int i = 0; i < static_cast<int>(clip->frames.size()); ++i) {
        slopengine::SpriteAnimFrame& animFrame = clip->frames[static_cast<std::size_t>(i)];
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        char idBuf[64] = {};
        std::snprintf(idBuf, sizeof(idBuf), "%s", animFrame.id.c_str());
        if (ImGui::BeginCombo("##animframeid", idBuf)) {
            for (const slopengine::SpriteFrame& sprFrame : editor.doc.asset.frames) {
                const bool selected = sprFrame.id == animFrame.id;
                if (ImGui::Selectable(sprFrame.id.c_str(), selected)) {
                    animFrame.id = sprFrame.id;
                    editor.doc.animDirty = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        int durationMs = static_cast<int>(std::lround(static_cast<double>(animFrame.duration) * 1000.0));
        if (ImGui::DragInt("##dur", &durationMs, 1.0f, 1, 60000, "%dms")) {
            animFrame.duration = static_cast<float>(durationMs) / 1000.0f;
            editor.doc.animDirty = true;
            editor.doc.animDuration = editor.clipDuration(editor.doc.animClip);
        }

        if (ImGui::Checkbox("R", &animFrame.tweenRotation)) {
            editor.doc.animDirty = true;
            editor.scrubAnim(editor.doc.animTime);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("S", &animFrame.tweenScale)) {
            editor.doc.animDirty = true;
            editor.scrubAnim(editor.doc.animTime);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("T", &animFrame.tweenTranslate)) {
            editor.doc.animDirty = true;
            editor.scrubAnim(editor.doc.animTime);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i - 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dn") && i + 1 < static_cast<int>(clip->frames.size())) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i + 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X") && clip->frames.size() > 1) {
            clip->frames.erase(clip->frames.begin() + i);
            editor.doc.animDirty = true;
            editor.doc.animDuration = editor.clipDuration(editor.doc.animClip);
            ImGui::PopID();
            break;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        char soundBuf[128] = {};
        std::snprintf(soundBuf, sizeof(soundBuf), "%s", animFrame.sound.c_str());
        if (ImGui::InputTextWithHint("##sound", "sound path", soundBuf, sizeof(soundBuf))) {
            animFrame.sound = soundBuf;
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Pick")) {
            soundBrowser.rescan(assets);
            soundBrowser.filter.clear();
            soundBrowser.open = true;
            soundPickFrameIndex = i;
        }
        if (animFrame.hasSound()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##soundvol", &animFrame.soundVolume, 0.01f, 0.0f, 2.0f, "vol %.2f")) {
                editor.doc.animDirty = true;
            }
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    if (soundBrowser.open) {
        std::string picked;
        if (soundBrowser.drawModal(picked)) {
            if (soundPickFrameIndex >= 0 &&
                soundPickFrameIndex < static_cast<int>(clip->frames.size())) {
                clip->frames[static_cast<std::size_t>(soundPickFrameIndex)].sound = std::move(picked);
                editor.doc.animDirty = true;
            }
            soundPickFrameIndex = -1;
        }
        if (!soundBrowser.open) {
            soundPickFrameIndex = -1;
        }
    }
}

void drawFramesSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::TextureBrowser& textureBrowser) {
    if (ImGui::Button("Add empty")) {
        slopengine::SpriteFrame frame{};
        frame.id = "F" + std::to_string(editor.doc.asset.frames.size());
        editor.doc.asset.frames.push_back(std::move(frame));
        editor.selectFrameIndex(static_cast<int>(editor.doc.asset.frames.size()) - 1);
        editor.markDirty();
        editor.doc.atlasDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate selected")) {
        editor.duplicateSelectedFrame();
    }

    for (int i = 0; i < static_cast<int>(editor.doc.asset.frames.size()); ++i) {
        slopengine::SpriteFrame& frame = editor.doc.asset.frames[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool selected = i == editor.doc.selectedFrameIndex;
        if (ImGui::Selectable("##framesel", selected, 0, ImVec2(18.0f, 0.0f))) {
            editor.selectFrameIndex(i);
        }
        ImGui::SameLine();
        char idBuf[64] = {};
        std::snprintf(idBuf, sizeof(idBuf), "%s", frame.id.c_str());
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputText("##frameid", idBuf, sizeof(idBuf))) {
            frame.id = idBuf;
            if (selected) {
                editor.doc.currentFrame = frame.id;
            }
            editor.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X") && editor.doc.asset.frames.size() > 1) {
            editor.doc.asset.frames.erase(editor.doc.asset.frames.begin() + i);
            editor.selectFrameIndex(std::min(i, static_cast<int>(editor.doc.asset.frames.size()) - 1));
            editor.markDirty();
            editor.doc.atlasDirty = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size())) {
        return;
    }

    slopengine::SpriteFrame& frame =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
    ImGui::Separator();
    ImGui::Text("Rotations for \"%s\"", frame.id.c_str());

    const slopsprite::FrameRotationMode detected = slopsprite::detectFrameRotationMode(frame);
    const char* modeLabels[] = {"No rotations", "5 Angles", "8 Angles"};
    const char* modePreview = detected == slopsprite::FrameRotationMode::Custom
                                  ? "Custom"
                                  : modeLabels[static_cast<int>(detected)];
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##rotmode", modePreview)) {
        for (int i = 0; i < 3; ++i) {
            const bool selected = static_cast<int>(detected) == i;
            if (ImGui::Selectable(modeLabels[i], selected)) {
                const auto next = static_cast<slopsprite::FrameRotationMode>(i);
                slopsprite::applyFrameRotationMode(frame, next);
                editor.doc.selectedRot =
                    slopsprite::clampSelectedRotToMode(next, editor.doc.selectedRot);
                editor.markDirty();
                editor.doc.atlasDirty = true;
            }
        }
        ImGui::EndCombo();
    }

    const slopsprite::FrameRotationMode mode = slopsprite::detectFrameRotationMode(frame);
    editor.doc.selectedRot = slopsprite::clampSelectedRotToMode(mode, editor.doc.selectedRot);

    const int authorCount = slopsprite::frameRotationAuthorCount(mode);
    for (int slot = 0; slot < authorCount; ++slot) {
        if (slot > 0) {
            ImGui::SameLine();
        }
        const int rot = slopsprite::frameRotationAuthorIndex(mode, slot);
        ImGui::PushID(100 + rot);
        const bool filled =
            frame.rotations[rot].has_value() && !frame.rotations[rot]->texturePath.empty();
        const bool selected = editor.doc.selectedRot == rot;
        char label[8];
        if (mode == slopsprite::FrameRotationMode::None) {
            std::snprintf(label, sizeof(label), "0");
        } else {
            std::snprintf(label, sizeof(label), "%d", rot);
        }
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.55f, 0.75f, 1.0f));
        } else if (filled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.30f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(28.0f, 0.0f))) {
            editor.doc.selectedRot = rot;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    if (mode == slopsprite::FrameRotationMode::Five) {
        ImGui::TextDisabled("Angles 6–8 are mirrored from 4–2");
    }

    const int rot = editor.doc.selectedRot;
    char hitBuf[256] = {};
    bool mirror = false;
    std::string texPath;
    if (frame.rotations[rot].has_value()) {
        texPath = frame.rotations[rot]->texturePath;
        mirror = frame.rotations[rot]->mirror;
        if (frame.rotations[rot]->hitMaskPath.has_value()) {
            std::snprintf(
                hitBuf, sizeof(hitBuf), "%s", frame.rotations[rot]->hitMaskPath->c_str());
        }
    }

    auto ensureRot = [&]() -> slopengine::SpriteRotation& {
        if (!frame.rotations[rot].has_value()) {
            frame.rotations[rot] = slopengine::SpriteRotation{};
        }
        return *frame.rotations[rot];
    };

    auto afterRotEdit = [&]() {
        if (mode == slopsprite::FrameRotationMode::Five) {
            slopsprite::syncFiveAngleMirrors(frame);
        }
        editor.markDirty();
        editor.doc.atlasDirty = true;
    };

    ImGui::TextWrapped("%s", texPath.empty() ? "(no texture)" : texPath.c_str());
    if (ImGui::Button("Pick texture", ImVec2(-1.0f, 0.0f))) {
        textureBrowser.rescan(assets);
        textureBrowser.open = true;
    }
    std::string picked;
    if (textureBrowser.drawModal(picked)) {
        ensureRot().texturePath = picked;
        editor.rebuildAtlas(assets);
        slopengine::SpriteRotation& entry = ensureRot();
        if (!entry.hasOffset) {
            const int w = std::max(1, entry.pixelWidth);
            const int h = std::max(1, entry.pixelHeight);
            entry.offsetX = w / 2;
            entry.offsetY = h;
            entry.hasOffset = true;
        }
        afterRotEdit();
    }

    if (mode == slopsprite::FrameRotationMode::Custom ||
        mode == slopsprite::FrameRotationMode::Eight) {
        if (ImGui::Checkbox("Mirror", &mirror)) {
            ensureRot().mirror = mirror;
            afterRotEdit();
        }
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("Hit mask", hitBuf, sizeof(hitBuf))) {
        slopengine::SpriteRotation& entry = ensureRot();
        if (hitBuf[0] == '\0') {
            entry.hitMaskPath.reset();
        } else {
            entry.hitMaskPath = hitBuf;
        }
        afterRotEdit();
    }

    if (frame.rotations[rot].has_value()) {
        slopengine::SpriteRotation& entry = *frame.rotations[rot];
        if (ImGui::DragFloat("Rotation", &entry.rotationDeg, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
            afterRotEdit();
        }
        float scale[2] = {entry.scaleX, entry.scaleY};
        if (ImGui::DragFloat2("Scale", scale, 0.01f, 0.01f, 16.0f, "%.2f")) {
            entry.scaleX = scale[0];
            entry.scaleY = scale[1];
            afterRotEdit();
        }
        float translate[2] = {entry.translateX, entry.translateY};
        if (ImGui::DragFloat2("Translate", translate, 0.5f, 0.0f, 0.0f, "%.1f")) {
            entry.translateX = translate[0];
            entry.translateY = translate[1];
            afterRotEdit();
        }
        if (ImGui::Button("Reset translate")) {
            entry.translateX = 0.0f;
            entry.translateY = 0.0f;
            afterRotEdit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset scale")) {
            entry.scaleX = 1.0f;
            entry.scaleY = 1.0f;
            afterRotEdit();
        }
    }
}

void drawOnionSection(slopsprite::Editor& editor) {
    ImGui::Checkbox("Compare", &editor.doc.onionEnabled);
    if (editor.doc.onionEnabled && !editor.doc.asset.frames.empty()) {
        editor.doc.onionFrameIndex = std::clamp(
            editor.doc.onionFrameIndex,
            0,
            static_cast<int>(editor.doc.asset.frames.size()) - 1);
        const char* onionPreview =
            editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)]
                .id.c_str();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##onionframe", onionPreview)) {
            for (int i = 0; i < static_cast<int>(editor.doc.asset.frames.size()); ++i) {
                const bool selected = i == editor.doc.onionFrameIndex;
                if (ImGui::Selectable(
                        editor.doc.asset.frames[static_cast<std::size_t>(i)].id.c_str(),
                        selected)) {
                    editor.doc.onionFrameIndex = i;
                }
            }
            ImGui::EndCombo();
        }
        const slopsprite::FrameRotationMode onionMode = slopsprite::detectFrameRotationMode(
            editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)]);
        editor.doc.onionRot = slopsprite::clampSelectedRotToMode(onionMode, editor.doc.onionRot);
        if (onionMode == slopsprite::FrameRotationMode::None) {
            ImGui::TextDisabled("Onion rot: 0");
        } else if (onionMode == slopsprite::FrameRotationMode::Five) {
            ImGui::SliderInt("Onion rot", &editor.doc.onionRot, 1, 5);
        } else {
            ImGui::SliderInt("Onion rot", &editor.doc.onionRot, 1, 8);
        }
    }
    ImGui::DragFloat("Align zoom", &editor.doc.alignZoom, 0.05f, 0.25f, 16.0f, "%.2f");
}

void drawInspector(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::TextureBrowser& textureBrowser,
    slopsprite::SoundBrowser& soundBrowser) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("Select a sprite");
        return;
    }

    ImGui::Text("Path: %s", editor.doc.virtualPath.c_str());
    ImGui::Text("Dirty: %s", editor.doc.dirty ? "yes" : "no");

    float texel = editor.doc.asset.pixelsPerMeter;
    if (ImGui::DragFloat("Texel size", &texel, 0.5f, 1.0f, 512.0f, "%.1f")) {
        editor.doc.asset.pixelsPerMeter = texel;
        editor.markDirty();
    }

    const bool showOnion = editor.mode == slopsprite::PreviewMode::Align;
    static bool sectionOpen[3] = {true, true, true};
    const int sectionCount = showOnion ? 3 : 2;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float headerH = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    int openCount = 0;
    for (int i = 0; i < sectionCount; ++i) {
        if (sectionOpen[i]) {
            ++openCount;
        }
    }
    const float avail = ImGui::GetContentRegionAvail().y;
    const float bodyBudget = std::max(0.0f, avail - headerH * static_cast<float>(sectionCount));
    const float bodyH = openCount > 0 ? bodyBudget / static_cast<float>(openCount) : 0.0f;

    sectionOpen[0] = collapsingHeaderWithIcon(
        assets, kDefaultIconSet, "images", "Frames", ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[0]) {
        if (ImGui::BeginChild("##sprframes", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
            drawFramesSection(editor, assets, textureBrowser);
        }
        ImGui::EndChild();
    }

    sectionOpen[1] = collapsingHeaderWithIcon(
        assets, kDefaultIconSet, "film", "Clip frames", ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[1]) {
        if (ImGui::BeginChild("##clipframes", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
            drawClipFramesSection(editor, assets, soundBrowser);
        }
        ImGui::EndChild();
    }

    if (showOnion) {
        sectionOpen[2] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "layers", "Onion skin", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[2]) {
            if (ImGui::BeginChild("##onion", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                drawOnionSection(editor);
            }
            ImGui::EndChild();
        }
    }

    if (editor.doc.atlasDirty) {
        editor.rebuildAtlas(assets);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const auto config = parseArgs(argc, argv);
    if (!config) {
        printUsage();
        return 1;
    }

    SetTraceLogLevel(LOG_INFO);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, "slopsprite");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopsprite: --target must be one of the mounted packages\n";
        CloseWindow();
        return 1;
    }

    slopengine::setupImGuiWithUiFont(assets, slopengine::kDefaultUiFontPath, true);

    slopsprite::Editor editor;
    editor.targetRoot = config->target;
    if (auto meta = slopengine::loadPackageMetaFile(config->target / "package.meta")) {
        editor.targetPackageId = meta->id;
    }
    slopsprite::loadViewCanvasSize(assets, editor.viewCanvasW, editor.viewCanvasH);

    slopsprite::SpriteBrowser browser;
    browser.rescan(assets);
    slopsprite::TextureBrowser textureBrowser;
    slopsprite::SoundBrowser soundBrowser;
    slopsprite::WorldPreview worldPreview;
    slopsprite::FpPreview fpPreview;
    slopsprite::AlignPreview alignPreview;
    slopengine::AudioWorld audioWorld;
    audioWorld.init();
    RenderTexture2D contentTarget{};
    bool quit = false;

    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }

        if (editor.doc.atlasDirty && editor.doc.open) {
            editor.rebuildAtlas(assets);
        }
        editor.tickAnim(GetFrameTime(), assets, &audioWorld);
        if (editor.statusTimer > 0.0f) {
            editor.statusTimer -= GetFrameTime();
            if (editor.statusTimer <= 0.0f) {
                editor.statusMessage.clear();
            }
        }

        rlImGuiBegin();
        ImGuiIO& io = ImGui::GetIO();
        const bool uiWantsMouse = io.WantCaptureMouse;
        const bool uiWantsKeyboard = io.WantCaptureKeyboard;

        const float chromeHeight = ImGui::GetFrameHeight();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        constexpr float kAnimHeight = 72.0f;
        const slopsprite::UiLayout layout =
            slopsprite::computeUiLayout(chromeHeight, statusHeight, kAnimHeight);
        slopsprite::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopsprite::pointInRect(mouse, layout.content);
        const bool allowPreviewInput =
            !uiWantsKeyboard && !uiWantsMouse && mouseInContent;

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save", "Ctrl+S", false, editor.doc.open)) {
                    editor.save(assets);
                    browser.rescan(assets);
                }
                if (ImGui::MenuItem("Rescan Sprites")) {
                    browser.rescan(assets);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem(
                        "World", nullptr, editor.mode == slopsprite::PreviewMode::World)) {
                    editor.mode = slopsprite::PreviewMode::World;
                }
                if (ImGui::MenuItem(
                        "First Person",
                        nullptr,
                        editor.mode == slopsprite::PreviewMode::FirstPerson)) {
                    editor.mode = slopsprite::PreviewMode::FirstPerson;
                }
                if (ImGui::MenuItem(
                        "Align", nullptr, editor.mode == slopsprite::PreviewMode::Align)) {
                    editor.mode = slopsprite::PreviewMode::Align;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S) && editor.doc.open) {
            editor.save(assets);
            browser.rescan(assets);
        }

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        ImGui::Begin(
            "Browser",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        browser.draw(editor, assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        ImGui::Begin(
            "Inspector",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        if (ImGui::RadioButton(
                "World", editor.mode == slopsprite::PreviewMode::World)) {
            editor.mode = slopsprite::PreviewMode::World;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "FP", editor.mode == slopsprite::PreviewMode::FirstPerson)) {
            editor.mode = slopsprite::PreviewMode::FirstPerson;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Align", editor.mode == slopsprite::PreviewMode::Align)) {
            editor.mode = slopsprite::PreviewMode::Align;
        }
        ImGui::Separator();
        drawInspector(editor, assets, textureBrowser, soundBrowser);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.animPanel.x, layout.animPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.animPanel.width, layout.animPanel.height));
        ImGui::Begin(
            "Animation",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar);
        drawAnimBar(editor);
        ImGui::End();

        if (editor.mode == slopsprite::PreviewMode::World) {
            worldPreview.draw(editor, contentTarget, allowPreviewInput);
        } else if (editor.mode == slopsprite::PreviewMode::FirstPerson) {
            fpPreview.draw(editor, contentTarget, layout.content, allowPreviewInput);
        } else {
            alignPreview.draw(editor, contentTarget, layout.content, allowPreviewInput);
        }

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        slopsprite::drawContentTarget(contentTarget, layout.content);

        const char* modeLabel = "World";
        if (editor.mode == slopsprite::PreviewMode::FirstPerson) {
            modeLabel = "FP";
        } else if (editor.mode == slopsprite::PreviewMode::Align) {
            modeLabel = "Align";
        }
        std::string statusLine = "target: ";
        statusLine += editor.targetPackageId.empty() ? editor.targetRoot.string()
                                                     : editor.targetPackageId;
        statusLine += " | mode: ";
        statusLine += modeLabel;
        statusLine += " | canvas: ";
        statusLine += std::to_string(editor.viewCanvasW);
        statusLine += "x";
        statusLine += std::to_string(editor.viewCanvasH);
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
        DrawText(
            statusLine.c_str(),
            8,
            GetScreenHeight() - static_cast<int>(statusHeight) + 4,
            16,
            LIGHTGRAY);

        rlImGuiEnd();
        EndDrawing();
    }

    if (contentTarget.id != 0) {
        UnloadRenderTexture(contentTarget);
    }
    slopengine::unloadSpriteAtlas(editor.doc.atlas);
    audioWorld.deinit();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
