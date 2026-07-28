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

using slopengine::beginMenuWithIcon;
using slopengine::buttonWithIcon;
using slopengine::collapsingHeaderWithIcon;
using slopengine::drawIconImGui;
using slopengine::kDefaultIconSet;
using slopengine::menuItemWithIcon;

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

void drawAnimBar(slopsprite::Editor& editor, slopengine::AssetStore& assets) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("No sprite open");
        return;
    }
    if (!editor.doc.hasAnim || editor.doc.animBank.clips.empty()) {
        ImGui::TextDisabled("No .spanim — create one in the Clip frames panel");
        return;
    }

    constexpr const char* kIcons = kDefaultIconSet;

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
    if (buttonWithIcon(
            assets,
            kIcons,
            editor.doc.animPlaying ? "control_pause" : "control_play",
            editor.doc.animPlaying ? "Pause" : "Play")) {
        if (editor.doc.animPlaying) {
            editor.stopAnim();
        } else {
            editor.playAnimClip(editor.doc.animClip, editor.doc.animLoop);
        }
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kIcons, "control_stop", "Stop")) {
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
    constexpr const char* kIcons = kDefaultIconSet;

    if (!editor.doc.hasAnim || editor.doc.animBank.clips.empty()) {
        ImGui::TextDisabled("No .spanim for this sprite");
        if (buttonWithIcon(assets, kIcons, "page_add", "Create .spanim", ImVec2(-1.0f, 0.0f))) {
            editor.ensureAnimBank();
        }
        return;
    }

    slopengine::SpriteAnimClip* clip = editor.currentAnimClip();
    if (clip == nullptr) {
        ImGui::TextDisabled("Select a clip in the timeline");
        return;
    }

    if (buttonWithIcon(
            assets, kIcons, "film_add", "Append selected .spr frame", ImVec2(-1.0f, 0.0f))) {
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
        drawIconImGui(assets, kIcons, "arrow_up");
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && i > 0) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i - 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        drawIconImGui(assets, kIcons, "arrow_down");
        ImGui::SameLine();
        if (ImGui::SmallButton("Dn") && i + 1 < static_cast<int>(clip->frames.size())) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i + 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        drawIconImGui(assets, kIcons, "cross");
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
        drawIconImGui(assets, kIcons, "sound");
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

        ImGui::SetNextItemWidth(-1.0f);
        std::string hintsJoined;
        for (std::size_t hi = 0; hi < animFrame.hints.size(); ++hi) {
            if (hi > 0) {
                hintsJoined.push_back(' ');
            }
            hintsJoined += animFrame.hints[hi];
        }
        char hintsBuf[256] = {};
        std::snprintf(hintsBuf, sizeof(hintsBuf), "%s", hintsJoined.c_str());
        if (ImGui::InputTextWithHint("##hints", "hints (space-separated)", hintsBuf, sizeof(hintsBuf))) {
            animFrame.hints.clear();
            const char* cursor = hintsBuf;
            while (*cursor != '\0') {
                while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
                    ++cursor;
                }
                if (*cursor == '\0') {
                    break;
                }
                const char* begin = cursor;
                while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != ',') {
                    ++cursor;
                }
                animFrame.hints.emplace_back(begin, static_cast<std::size_t>(cursor - begin));
            }
            editor.doc.animDirty = true;
        }

        ImGui::TextDisabled("Overlays (layer sprite clip x y)");
        for (int oi = 0; oi < static_cast<int>(animFrame.overlays.size()); ++oi) {
            slopengine::SpriteAnimOverlay& overlay =
                animFrame.overlays[static_cast<std::size_t>(oi)];
            ImGui::PushID(oi + 1000);
            const bool selected = editor.doc.selectedOverlayHoldIndex == i &&
                                  editor.doc.selectedOverlayIndex == oi;
            if (ImGui::SmallButton(selected ? "[*]" : "[ ]")) {
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
                editor.doc.muzzleSelected = false;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(48.0f);
            if (ImGui::DragInt("##layer", &overlay.layer, 0.2f)) {
                if (overlay.layer == 0) {
                    overlay.layer = 1;
                }
                editor.doc.animDirty = true;
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
            char spriteBuf[128] = {};
            std::snprintf(spriteBuf, sizeof(spriteBuf), "%s", overlay.sprite.c_str());
            if (ImGui::InputTextWithHint("##osprite", "sprite", spriteBuf, sizeof(spriteBuf))) {
                overlay.sprite = spriteBuf;
                editor.doc.animDirty = true;
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
            char clipBuf[64] = {};
            std::snprintf(clipBuf, sizeof(clipBuf), "%s", overlay.clip.c_str());
            if (ImGui::InputTextWithHint("##oclip", "clip", clipBuf, sizeof(clipBuf))) {
                overlay.clip = clipBuf;
                editor.doc.animDirty = true;
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            if (ImGui::DragFloat("##ox", &overlay.x, 0.5f, 0.0f, 0.0f, "x %.1f")) {
                editor.doc.animDirty = true;
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
            if (ImGui::DragFloat("##oy", &overlay.y, 0.5f, 0.0f, 0.0f, "y %.1f")) {
                editor.doc.animDirty = true;
                editor.doc.selectedOverlayHoldIndex = i;
                editor.doc.selectedOverlayIndex = oi;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                if (editor.doc.selectedOverlayHoldIndex == i &&
                    editor.doc.selectedOverlayIndex == oi) {
                    editor.doc.selectedOverlayHoldIndex = -1;
                    editor.doc.selectedOverlayIndex = -1;
                }
                animFrame.overlays.erase(animFrame.overlays.begin() + oi);
                editor.doc.animDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Add overlay")) {
            slopengine::SpriteAnimOverlay overlay{};
            overlay.layer = 1;
            animFrame.overlays.push_back(std::move(overlay));
            editor.doc.selectedOverlayHoldIndex = i;
            editor.doc.selectedOverlayIndex = static_cast<int>(animFrame.overlays.size()) - 1;
            editor.doc.animDirty = true;
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    if (soundBrowser.open) {
        std::string picked;
        if (soundBrowser.drawModal(assets, picked)) {
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

void drawFramesListSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;

    if (buttonWithIcon(assets, kIcons, "image_add", "Add empty")) {
        slopengine::SpriteFrame frame{};
        frame.id = "F" + std::to_string(editor.doc.asset.frames.size());
        editor.doc.asset.frames.push_back(std::move(frame));
        editor.selectFrameIndex(static_cast<int>(editor.doc.asset.frames.size()) - 1);
        editor.markDirty();
        editor.doc.atlasDirty = true;
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kIcons, "page_copy", "Duplicate selected")) {
        editor.duplicateSelectedFrame();
    }

    for (int i = 0; i < static_cast<int>(editor.doc.asset.frames.size()); ++i) {
        slopengine::SpriteFrame& frame = editor.doc.asset.frames[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool selected = i == editor.doc.selectedFrameIndex;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.40f, 0.65f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.48f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.35f, 0.58f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.26f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
        }
        const float pickSize = ImGui::GetFrameHeight();
        const ImVec2 pickPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##pick", ImVec2(pickSize, pickSize))) {
            editor.selectFrameIndex(i);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show keyed pose for this frame");
        }
        {
            const ImVec2 iconPos{
                pickPos.x + (pickSize - 16.0f) * 0.5f,
                pickPos.y + (pickSize - 16.0f) * 0.5f,
            };
            ImGui::SetCursorScreenPos(iconPos);
            drawIconImGui(assets, kIcons, "images", 16.0f);
            ImGui::SetCursorScreenPos(
                ImVec2(pickPos.x + pickSize + ImGui::GetStyle().ItemSpacing.x, pickPos.y));
        }
        ImGui::SameLine(0.0f, 0.0f);
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
        drawIconImGui(assets, kIcons, "cross");
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
}

void drawAlignFrameSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::TextureBrowser& textureBrowser) {
    constexpr const char* kIcons = kDefaultIconSet;

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size())) {
        ImGui::TextDisabled("Select a frame");
        return;
    }

    slopengine::SpriteFrame& frame =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
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
    if (buttonWithIcon(assets, kIcons, "folder_page", "Pick texture", ImVec2(-1.0f, 0.0f))) {
        textureBrowser.rescan(assets);
        textureBrowser.open = true;
    }
    std::string picked;
    if (textureBrowser.drawModal(assets, picked)) {
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

    if (!frame.rotations[rot].has_value()) {
        return;
    }

    slopengine::SpriteRotation& entry = *frame.rotations[rot];
    if (!entry.hasOffset) {
        const int w = std::max(1, entry.pixelWidth);
        const int h = std::max(1, entry.pixelHeight);
        entry.offsetX = w / 2;
        entry.offsetY = h;
        entry.hasOffset = true;
    }
    int offset[2] = {entry.offsetX, entry.offsetY};
    if (ImGui::DragInt2("Offset", offset, 1.0f)) {
        entry.offsetX = offset[0];
        entry.offsetY = offset[1];
        entry.hasOffset = true;
        afterRotEdit();
    }
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
    if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset translate")) {
        entry.translateX = 0.0f;
        entry.translateY = 0.0f;
        afterRotEdit();
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset scale")) {
        entry.scaleX = 1.0f;
        entry.scaleY = 1.0f;
        afterRotEdit();
    }
}

void drawAnimFrameSection(slopsprite::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size())) {
        ImGui::TextDisabled("Select a frame");
        return;
    }

    slopengine::SpriteFrame& frame =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
    ImGui::Text("Anim transforms for \"%s\"", frame.id.c_str());

    const slopsprite::FrameRotationMode mode = slopsprite::detectFrameRotationMode(frame);
    editor.doc.selectedRot = slopsprite::clampSelectedRotToMode(mode, editor.doc.selectedRot);

    const int authorCount = slopsprite::frameRotationAuthorCount(mode);
    for (int slot = 0; slot < authorCount; ++slot) {
        if (slot > 0) {
            ImGui::SameLine();
        }
        const int rot = slopsprite::frameRotationAuthorIndex(mode, slot);
        ImGui::PushID(200 + rot);
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

    const int rot = editor.doc.selectedRot;
    if (!frame.rotations[rot].has_value()) {
        ImGui::TextDisabled("Selected rot has no texture — set it up in Align");
        return;
    }

    slopengine::SpriteRotation& entry = *frame.rotations[rot];
    auto afterAnimEdit = [&]() {
        if (mode == slopsprite::FrameRotationMode::Five) {
            slopsprite::syncFiveAngleMirrors(frame);
        }
        editor.markDirty();
    };

    if (ImGui::DragFloat("Anim rotation", &entry.animRotationDeg, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
        afterAnimEdit();
    }
    float animScale[2] = {entry.animScaleX, entry.animScaleY};
    if (ImGui::DragFloat2("Anim scale", animScale, 0.01f, 0.01f, 16.0f, "%.2f")) {
        entry.animScaleX = animScale[0];
        entry.animScaleY = animScale[1];
        afterAnimEdit();
    }
    float animTranslate[2] = {entry.animTranslateX, entry.animTranslateY};
    if (ImGui::DragFloat2("Anim translate", animTranslate, 0.5f, 0.0f, 0.0f, "%.1f")) {
        entry.animTranslateX = animTranslate[0];
        entry.animTranslateY = animTranslate[1];
        afterAnimEdit();
    }
    if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset anim translate")) {
        entry.animTranslateX = 0.0f;
        entry.animTranslateY = 0.0f;
        afterAnimEdit();
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset anim scale")) {
        entry.animScaleX = 1.0f;
        entry.animScaleY = 1.0f;
        afterAnimEdit();
    }

    if (mode == slopsprite::FrameRotationMode::Five ||
        mode == slopsprite::FrameRotationMode::Eight) {
        auto copyAnimFieldToOtherRots = [&](auto apply) {
            bool changed = false;
            for (int slot = 0; slot < authorCount; ++slot) {
                const int dstRot = slopsprite::frameRotationAuthorIndex(mode, slot);
                if (dstRot == rot || !frame.rotations[dstRot].has_value()) {
                    continue;
                }
                apply(*frame.rotations[dstRot]);
                changed = true;
            }
            if (changed) {
                afterAnimEdit();
            }
        };

        if (buttonWithIcon(assets, kIcons, "shape_rotate_clockwise", "Copy rot")) {
            const float value = entry.animRotationDeg;
            copyAnimFieldToOtherRots([&](slopengine::SpriteRotation& dst) {
                dst.animRotationDeg = value;
            });
        }
        ImGui::SameLine();
        if (buttonWithIcon(assets, kIcons, "shape_handles", "Copy scale")) {
            const float sx = entry.animScaleX;
            const float sy = entry.animScaleY;
            copyAnimFieldToOtherRots([&](slopengine::SpriteRotation& dst) {
                dst.animScaleX = sx;
                dst.animScaleY = sy;
            });
        }
        ImGui::SameLine();
        if (buttonWithIcon(assets, kIcons, "arrow_inout", "Copy trans")) {
            const float tx = entry.animTranslateX;
            const float ty = entry.animTranslateY;
            copyAnimFieldToOtherRots([&](slopengine::SpriteRotation& dst) {
                dst.animTranslateX = tx;
                dst.animTranslateY = ty;
            });
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

    if (ImGui::Checkbox("Fullbright", &editor.doc.asset.fullbright)) {
        editor.markDirty();
    }

    {
        const char* modeLabel = "Face";
        if (editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::Fixed) {
            modeLabel = "Fixed";
        } else if (editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::View) {
            modeLabel = "View";
        } else if (editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::Screen) {
            modeLabel = "Screen";
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("Billboard", modeLabel)) {
            if (ImGui::Selectable(
                    "Face",
                    editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::Face)) {
                editor.doc.asset.billboardMode = slopengine::SpriteBillboardMode::Face;
                editor.markDirty();
            }
            if (ImGui::Selectable(
                    "View",
                    editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::View)) {
                editor.doc.asset.billboardMode = slopengine::SpriteBillboardMode::View;
                editor.markDirty();
            }
            if (ImGui::Selectable(
                    "Screen",
                    editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::Screen)) {
                editor.doc.asset.billboardMode = slopengine::SpriteBillboardMode::Screen;
                editor.markDirty();
            }
            if (ImGui::Selectable(
                    "Fixed",
                    editor.doc.asset.billboardMode == slopengine::SpriteBillboardMode::Fixed)) {
                editor.doc.asset.billboardMode = slopengine::SpriteBillboardMode::Fixed;
                editor.markDirty();
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::CollapsingHeader("View defaults", ImGuiTreeNodeFlags_DefaultOpen)) {
        float canvas[2] = {editor.doc.viewSprite.canvasX, editor.doc.viewSprite.canvasY};
        if (ImGui::DragFloat2("Canvas", canvas, 0.5f, 0.0f, 0.0f, "%.1f")) {
            editor.doc.viewSprite.canvasX = canvas[0];
            editor.doc.viewSprite.canvasY = canvas[1];
            editor.markDirty();
        }
        float origin[2] = {editor.doc.viewSprite.originX, editor.doc.viewSprite.originY};
        if (ImGui::DragFloat2("Origin", origin, 0.01f, 0.0f, 1.0f, "%.2f")) {
            editor.doc.viewSprite.originX = origin[0];
            editor.doc.viewSprite.originY = origin[1];
            editor.markDirty();
        }
        float scale[2] = {editor.doc.viewSprite.scaleX, editor.doc.viewSprite.scaleY};
        if (ImGui::DragFloat2("Scale", scale, 0.01f, 0.01f, 8.0f, "%.2f")) {
            editor.doc.viewSprite.scaleX = scale[0];
            editor.doc.viewSprite.scaleY = scale[1];
            editor.markDirty();
        }
        if (ImGui::DragFloat("Rotation", &editor.doc.viewSprite.rotationDeg, 0.5f, -360.0f, 360.0f, "%.1f")) {
            editor.markDirty();
        }
        float eye[3] = {editor.doc.eyeOffsetX, editor.doc.eyeOffsetY, editor.doc.eyeOffsetZ};
        if (ImGui::DragFloat3("Eye offset", eye, 0.01f, 0.0f, 0.0f, "%.2f")) {
            editor.doc.eyeOffsetX = eye[0];
            editor.doc.eyeOffsetY = eye[1];
            editor.doc.eyeOffsetZ = eye[2];
            editor.markDirty();
        }
        if (ImGui::Checkbox("Muzzle tip", &editor.doc.hasMuzzle)) {
            editor.markDirty();
            if (!editor.doc.hasMuzzle) {
                editor.doc.muzzleSelected = false;
            }
        }
        if (editor.doc.hasMuzzle) {
            if (ImGui::SmallButton(editor.doc.muzzleSelected ? "[*] Select" : "[ ] Select")) {
                editor.doc.muzzleSelected = !editor.doc.muzzleSelected;
                if (editor.doc.muzzleSelected) {
                    editor.doc.selectedOverlayHoldIndex = -1;
                    editor.doc.selectedOverlayIndex = -1;
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("drag in FP preview");
            float muzzle[2] = {editor.doc.muzzleX, editor.doc.muzzleY};
            if (ImGui::DragFloat2("Muzzle XY", muzzle, 0.5f, 0.0f, 0.0f, "%.1f")) {
                editor.doc.muzzleX = muzzle[0];
                editor.doc.muzzleY = muzzle[1];
                editor.markDirty();
            }
            ImGui::TextDisabled("Canvas px from sprite pivot (same as overlay XY)");
        }
    }

    const bool isAlign = editor.mode == slopsprite::PreviewMode::Align;
    static bool alignOpen[3] = {true, true, true};
    static bool animOpen[3] = {true, true, true};
    bool* sectionOpen = isAlign ? alignOpen : animOpen;
    constexpr int kSectionCount = 3;

    const ImGuiStyle& style = ImGui::GetStyle();
    int openCount = 0;
    for (int i = 0; i < kSectionCount; ++i) {
        if (sectionOpen[i]) {
            ++openCount;
        }
    }
    const float avail = ImGui::GetContentRegionAvail().y;
    const float frameH = ImGui::GetFrameHeight();
    const float spacing = style.ItemSpacing.y;
    const float bodyBudget = std::max(
        0.0f,
        avail - static_cast<float>(kSectionCount) * frameH -
            static_cast<float>(kSectionCount + openCount - 1) * spacing);
    const float bodyH = openCount > 0 ? bodyBudget / static_cast<float>(openCount) : 0.0f;

    sectionOpen[0] = collapsingHeaderWithIcon(
        assets, kDefaultIconSet, "images", "Frames", ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[0]) {
        if (ImGui::BeginChild("##sprframes", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
            drawFramesListSection(editor, assets);
        }
        ImGui::EndChild();
    }

    if (isAlign) {
        sectionOpen[1] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "picture_edit", "Base transforms", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[1]) {
            if (ImGui::BeginChild("##basetrans", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                drawAlignFrameSection(editor, assets, textureBrowser);
            }
            ImGui::EndChild();
        }

        sectionOpen[2] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "layers", "Onion skin", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[2]) {
            if (ImGui::BeginChild("##onion", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                drawOnionSection(editor);
            }
            ImGui::EndChild();
        }
    } else {
        sectionOpen[1] = collapsingHeaderWithIcon(
            assets,
            kDefaultIconSet,
            "arrow_rotate_clockwise",
            "Anim transforms",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[1]) {
            if (ImGui::BeginChild("##animtrans", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                drawAnimFrameSection(editor, assets);
            }
            ImGui::EndChild();
        }

        sectionOpen[2] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "film", "Clip frames", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[2]) {
            if (ImGui::BeginChild("##clipframes", ImVec2(0.0f, bodyH), ImGuiChildFlags_Borders)) {
                drawClipFramesSection(editor, assets, soundBrowser);
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
        if (editor.mode != slopsprite::PreviewMode::Align) {
            editor.tickAnim(GetFrameTime(), assets, &audioWorld);
        }
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
        const float animHeight =
            editor.mode == slopsprite::PreviewMode::Align ? 0.0f : 72.0f;
        const slopsprite::UiLayout layout =
            slopsprite::computeUiLayout(chromeHeight, statusHeight, animHeight);
        slopsprite::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopsprite::pointInRect(mouse, layout.content);
        const bool allowPreviewInput =
            !uiWantsKeyboard && !uiWantsMouse && mouseInContent;

        auto setPreviewMode = [&](slopsprite::PreviewMode mode) {
            if (mode == slopsprite::PreviewMode::Align &&
                editor.mode != slopsprite::PreviewMode::Align) {
                editor.stopAnim();
            }
            editor.mode = mode;
        };

        if (ImGui::BeginMainMenuBar()) {
            constexpr const char* kIcons = kDefaultIconSet;
            if (beginMenuWithIcon(assets, kIcons, "folder", "File")) {
                if (menuItemWithIcon(assets, kIcons, "page_add", "New Sprite...", "Ctrl+N")) {
                    editor.showNewSpriteModal = true;
                }
                if (menuItemWithIcon(assets, kIcons, "disk", "Save", "Ctrl+S", false, editor.doc.open)) {
                    editor.save(assets);
                    browser.rescan(assets);
                }
                if (menuItemWithIcon(assets, kIcons, "arrow_refresh", "Rescan Sprites")) {
                    browser.rescan(assets);
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "door", "Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "eye", "View")) {
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "world",
                        "World",
                        nullptr,
                        editor.mode == slopsprite::PreviewMode::World)) {
                    setPreviewMode(slopsprite::PreviewMode::World);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "user",
                        "First Person",
                        nullptr,
                        editor.mode == slopsprite::PreviewMode::FirstPerson)) {
                    setPreviewMode(slopsprite::PreviewMode::FirstPerson);
                }
                if (menuItemWithIcon(
                        assets,
                        kIcons,
                        "shape_square",
                        "Align",
                        nullptr,
                        editor.mode == slopsprite::PreviewMode::Align)) {
                    setPreviewMode(slopsprite::PreviewMode::Align);
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "bug", "Debug")) {
                if (beginMenuWithIcon(assets, kIcons, "film", "Sprites")) {
                    menuItemWithIcon(
                        assets, kIcons, "color_swatch", "Masks", nullptr, &editor.showSpriteMasks);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S) && editor.doc.open) {
            editor.save(assets);
            browser.rescan(assets);
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N)) {
            editor.showNewSpriteModal = true;
        }

        if (editor.showNewSpriteModal) {
            ImGui::OpenPopup("New Sprite");
            editor.showNewSpriteModal = false;
        }
        if (ImGui::BeginPopupModal("New Sprite", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            constexpr const char* kIcons = kDefaultIconSet;
            ImGui::TextUnformatted("Virtual path under sprites/ (no .spr)");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint(
                "##newsprpath", "e.g. monsters/imp", editor.newSpritePathBuf, sizeof(editor.newSpritePathBuf));
            if (editor.doc.dirty) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Current sprite has unsaved changes.");
            }
            const char* createLabel = editor.doc.dirty ? "Discard & Create" : "Create";
            if (buttonWithIcon(assets, kIcons, "page_add", createLabel, ImVec2(160, 0))) {
                if (editor.newSprite(editor.newSpritePathBuf)) {
                    editor.newSpritePathBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("Browser", nullptr, panelFlags);
        browser.draw(editor, assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        ImGui::Begin("Inspector", nullptr, panelFlags);
        if (ImGui::BeginTabBar("##previewTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            auto previewTabButton = [&](const char* label, slopsprite::PreviewMode mode) {
                const bool selected = editor.mode == mode;
                if (selected) {
                    ImGui::PushStyleColor(
                        ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                    ImGui::PushStyleColor(
                        ImGuiCol_TabHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                }
                if (ImGui::TabItemButton(label)) {
                    setPreviewMode(mode);
                }
                if (selected) {
                    ImGui::PopStyleColor(2);
                }
            };
            previewTabButton("World", slopsprite::PreviewMode::World);
            previewTabButton("FP", slopsprite::PreviewMode::FirstPerson);
            previewTabButton("Align", slopsprite::PreviewMode::Align);
            ImGui::EndTabBar();
        }
        drawInspector(editor, assets, textureBrowser, soundBrowser);
        ImGui::End();

        if (animHeight > 0.0f) {
            ImGui::SetNextWindowPos(ImVec2(layout.animPanel.x, layout.animPanel.y));
            ImGui::SetNextWindowSize(ImVec2(layout.animPanel.width, layout.animPanel.height));
            ImGui::Begin(
                "Animation",
                nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoTitleBar);
            drawAnimBar(editor, assets);
            ImGui::End();
        }

        const ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

        if (editor.mode == slopsprite::PreviewMode::World) {
            ImGui::SetNextWindowPos(
                ImVec2(layout.content.x + 8.0f, layout.content.y + 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.72f);
            if (ImGui::Begin("##worldoverlay", nullptr, overlayFlags)) {
                constexpr const char* kIcons = kDefaultIconSet;
                if (buttonWithIcon(
                        assets,
                        kIcons,
                        worldPreview.autoOrbit ? "control_pause" : "arrow_rotate_clockwise",
                        worldPreview.autoOrbit ? "Stop" : "Spin")) {
                    worldPreview.autoOrbit = !worldPreview.autoOrbit;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat(
                    "##orbitspeed",
                    &worldPreview.autoOrbitSpeedDeg,
                    1.0f,
                    -360.0f,
                    360.0f,
                    "%.0f deg/s");
                ImGui::TextDisabled("RMB orbit  Wheel zoom");
                if (editor.doc.open) {
                    const char* pose =
                        slopsprite::previewPoseLabel(editor.doc, slopsprite::PreviewMode::World);
                    if (pose[0] != '\0') {
                        const Color poseColor = slopsprite::previewPoseLabelColor(
                            editor.doc, slopsprite::PreviewMode::World);
                        ImGui::TextColored(
                            ImVec4(
                                poseColor.r / 255.0f,
                                poseColor.g / 255.0f,
                                poseColor.b / 255.0f,
                                poseColor.a / 255.0f),
                            "%s",
                            pose);
                    }
                }
                if (editor.showSpriteMasks && !editor.doc.asset.hitParts.empty()) {
                    static constexpr ImVec4 kPartColors[] = {
                        {80 / 255.0f, 180 / 255.0f, 255 / 255.0f, 1.0f},
                        {80 / 255.0f, 255 / 255.0f, 120 / 255.0f, 1.0f},
                        {255 / 255.0f, 80 / 255.0f, 80 / 255.0f, 1.0f},
                        {255 / 255.0f, 220 / 255.0f, 40 / 255.0f, 1.0f},
                        {220 / 255.0f, 80 / 255.0f, 255 / 255.0f, 1.0f},
                        {40 / 255.0f, 255 / 255.0f, 220 / 255.0f, 1.0f},
                    };
                    ImGui::Separator();
                    ImGui::TextUnformatted("Hit parts");
                    for (std::size_t i = 0; i < editor.doc.asset.hitParts.size(); ++i) {
                        const auto& part = editor.doc.asset.hitParts[i];
                        const ImVec4 color =
                            kPartColors[i % (sizeof(kPartColors) / sizeof(kPartColors[0]))];
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::ColorButton(
                            "##hitpart",
                            color,
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                            ImVec2(12.0f, 12.0f));
                        ImGui::SameLine();
                        ImGui::TextColored(color, "%s", part.name.c_str());
                        ImGui::PopID();
                    }
                }
            }
            ImGui::End();

            worldPreview.draw(editor, assets, contentTarget, allowPreviewInput);
        } else if (editor.mode == slopsprite::PreviewMode::FirstPerson) {
            ImGui::SetNextWindowPos(
                ImVec2(layout.content.x + 8.0f, layout.content.y + 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.72f);
            if (ImGui::Begin("##fpoverlay", nullptr, overlayFlags)) {
                ImGui::TextDisabled("FP");
                if (editor.doc.open) {
                    const char* pose = slopsprite::previewPoseLabel(
                        editor.doc, slopsprite::PreviewMode::FirstPerson);
                    if (pose[0] != '\0') {
                        const Color poseColor = slopsprite::previewPoseLabelColor(
                            editor.doc, slopsprite::PreviewMode::FirstPerson);
                        ImGui::TextColored(
                            ImVec4(
                                poseColor.r / 255.0f,
                                poseColor.g / 255.0f,
                                poseColor.b / 255.0f,
                                poseColor.a / 255.0f),
                            "%s",
                            pose);
                    }
                }
            }
            ImGui::End();

            fpPreview.draw(editor, assets, contentTarget, layout.content, allowPreviewInput);
        } else {
            ImGui::SetNextWindowPos(
                ImVec2(layout.content.x + 8.0f, layout.content.y + 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.72f);
            if (ImGui::Begin("##alignoverlay", nullptr, overlayFlags)) {
                if (!editor.doc.open || editor.doc.atlasDirty) {
                    ImGui::TextDisabled("Open a sprite to align");
                } else if (
                    editor.doc.selectedFrameIndex < 0 ||
                    editor.doc.selectedFrameIndex >=
                        static_cast<int>(editor.doc.asset.frames.size()) ||
                    editor.doc.selectedRot < 0 ||
                    editor.doc.selectedRot >= slopengine::kSpriteRotationCount ||
                    !editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)]
                         .rotations[editor.doc.selectedRot]
                         .has_value() ||
                    editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)]
                        .rotations[editor.doc.selectedRot]
                        ->texturePath.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.63f, 0.47f, 1.0f), "Selected rot has no texture");
                } else {
                    const slopengine::SpriteRotation& rot =
                        *editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)]
                             .rotations[editor.doc.selectedRot];
                    ImGui::TextDisabled(
                        "Align  wheel zoom  rot %d  off %d,%d",
                        editor.doc.selectedRot,
                        rot.hasOffset ? rot.offsetX : rot.pixelWidth / 2,
                        rot.hasOffset ? rot.offsetY : rot.pixelHeight);
                }
            }
            ImGui::End();

            alignPreview.draw(editor, contentTarget, layout.content, allowPreviewInput);
        }

        {
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

            ImGui::SetNextWindowPos(
                ImVec2(0.0f, static_cast<float>(GetScreenHeight()) - statusHeight),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(static_cast<float>(GetScreenWidth()), statusHeight), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            if (ImGui::Begin(
                    "##status",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
                ImGui::TextUnformatted(statusLine.c_str());
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        slopsprite::drawContentTarget(contentTarget, layout.content);

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
