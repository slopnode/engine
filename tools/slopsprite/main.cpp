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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using slopengine::beginMainMenuBar;
using slopengine::beginMenuWithIcon;
using slopengine::buttonWithIcon;
using slopengine::collapsingHeaderWithIcon;
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
        << "Usage: slopsprite --base-game <path> [--mod <path>]... --target <path>\n"
        << "\n"
        << "  --base-game   Base game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives sprite saves (required)\n"
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

constexpr float kLabelColumnWidth = 120.0f;

/** Draws @p label then positions the cursor for a left-labeled, full-width control. */
void labeledField(const char* label, float labelWidth = kLabelColumnWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
}

float iconButtonWidth() {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x;
}

bool deleteIconButton(slopengine::AssetStore& assets, const char* kIcons) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.30f, 0.30f, 1.0f));
    const bool pressed =
        slopengine::iconButton(assets, kIcons, "cross", ImVec2(iconButtonWidth(), 0.0f));
    ImGui::PopStyleColor();
    return pressed;
}

void addDashedLine(
    ImDrawList* drawList, ImVec2 p0, ImVec2 p1, ImU32 color, float thickness) {
    constexpr float kDashLen = 4.0f;
    constexpr float kGapLen = 3.0f;
    const float length = p1.x - p0.x;
    float drawn = 0.0f;
    bool dash = true;
    while (drawn < length) {
        const float seg = std::min(dash ? kDashLen : kGapLen, length - drawn);
        if (dash) {
            drawList->AddLine(
                ImVec2(p0.x + drawn, p0.y), ImVec2(p0.x + drawn + seg, p0.y), color, thickness);
        }
        drawn += seg;
        dash = !dash;
    }
}

/** Piano-roll-style Translate/Rotate/Scale tween tracks for the current clip. */
void drawTweenTracks(slopsprite::Editor& editor, const slopengine::SpriteAnimClip& clip) {
    if (clip.frames.empty()) {
        return;
    }

    constexpr float kLabelWidth = 16.0f;
    constexpr float kRowHeight = 14.0f;
    constexpr float kRowGap = 3.0f;
    constexpr int kTrackCount = 3;
    struct TrackDef {
        const char* label;
        ImU32 color;
    };
    static constexpr TrackDef kTracks[kTrackCount] = {
        {"T", IM_COL32(90, 170, 255, 255)},
        {"R", IM_COL32(255, 170, 60, 255)},
        {"S", IM_COL32(120, 220, 120, 255)},
    };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float trackX0 = origin.x + kLabelWidth;
    const float trackWidth = std::max(20.0f, ImGui::GetContentRegionAvail().x - kLabelWidth);
    const float totalHeight = kRowHeight * kTrackCount + kRowGap * (kTrackCount - 1);

    std::vector<float> startMs(clip.frames.size() + 1, 0.0f);
    for (std::size_t i = 0; i < clip.frames.size(); ++i) {
        startMs[i + 1] = startMs[i] + clip.frames[i].duration * 1000.0f;
    }
    const float clipDurMs = std::max(startMs.back(), 1.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (editor.doc.selectedClipFrameIndex >= 0 &&
        editor.doc.selectedClipFrameIndex < static_cast<int>(clip.frames.size())) {
        const std::size_t si = static_cast<std::size_t>(editor.doc.selectedClipFrameIndex);
        const float sx0 = trackX0 + (startMs[si] / clipDurMs) * trackWidth;
        const float sx1 = trackX0 + (startMs[si + 1] / clipDurMs) * trackWidth;
        drawList->AddRectFilled(
            ImVec2(sx0, origin.y),
            ImVec2(sx1, origin.y + totalHeight),
            IM_COL32(255, 255, 255, 30));
    }

    for (int t = 0; t < kTrackCount; ++t) {
        const float rowY0 = origin.y + static_cast<float>(t) * (kRowHeight + kRowGap);
        const float rowY1 = rowY0 + kRowHeight;
        const float midY = (rowY0 + rowY1) * 0.5f;
        drawList->AddText(
            ImVec2(origin.x, rowY0 + 1.0f), IM_COL32(170, 170, 178, 255), kTracks[t].label);
        for (std::size_t i = 0; i < clip.frames.size(); ++i) {
            const float x0 = trackX0 + (startMs[i] / clipDurMs) * trackWidth;
            const float x1 = trackX0 + (startMs[i + 1] / clipDurMs) * trackWidth;
            const bool on = t == 0 ? clip.frames[i].tweenTranslate
                : t == 1              ? clip.frames[i].tweenRotation
                                       : clip.frames[i].tweenScale;
            if (on) {
                drawList->AddLine(
                    ImVec2(x0 + 1.0f, midY), ImVec2(x1 - 1.0f, midY), kTracks[t].color, 3.0f);
            } else {
                addDashedLine(
                    drawList,
                    ImVec2(x0 + 1.0f, midY),
                    ImVec2(x1 - 1.0f, midY),
                    IM_COL32(90, 90, 98, 255),
                    1.5f);
            }
            drawList->AddLine(
                ImVec2(x0, rowY0), ImVec2(x0, rowY1), IM_COL32(55, 55, 60, 255), 1.0f);
        }

        constexpr float kKeyframeRadius = 3.0f;
        for (std::size_t j = 0; j <= clip.frames.size(); ++j) {
            const float x = trackX0 + (startMs[j] / clipDurMs) * trackWidth;
            drawList->AddCircleFilled(ImVec2(x, midY), kKeyframeRadius, IM_COL32(22, 22, 26, 255), 8);
            drawList->AddCircle(
                ImVec2(x, midY), kKeyframeRadius, IM_COL32(232, 232, 236, 255), 8, 1.3f);
        }
    }
    const float trackXEnd = trackX0 + trackWidth;
    drawList->AddLine(
        ImVec2(trackXEnd, origin.y),
        ImVec2(trackXEnd, origin.y + totalHeight),
        IM_COL32(55, 55, 60, 255),
        1.0f);

    const float playFrac = std::clamp(editor.doc.animTime * 1000.0f / clipDurMs, 0.0f, 1.0f);
    const float playX = trackX0 + playFrac * trackWidth;
    drawList->AddLine(
        ImVec2(playX, origin.y),
        ImVec2(playX, origin.y + totalHeight),
        IM_COL32(255, 255, 255, 220),
        1.5f);

    ImGui::InvisibleButton("##tweentracks", ImVec2(trackWidth + kLabelWidth, totalHeight));
    if (ImGui::IsItemActive()) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        const float frac = std::clamp((mouseX - trackX0) / trackWidth, 0.0f, 1.0f);
        editor.doc.animPlaying = false;
        editor.scrubAnim(frac * clipDurMs / 1000.0f);
    }
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

    if (const slopengine::SpriteAnimClip* clip = editor.currentAnimClip()) {
        drawTweenTracks(editor, *clip);
    }
}

void drawRotationSection(
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

    labeledField("Frame fullbright");
    if (ImGui::Checkbox("##framefullbright", &frame.fullbright)) {
        editor.markDirty();
    }

    const int rot = editor.doc.selectedRot;
    char hitBuf[256] = {};
    char brightBuf[256] = {};
    bool mirror = false;
    std::string texPath;
    if (frame.rotations[rot].has_value()) {
        texPath = frame.rotations[rot]->texturePath;
        mirror = frame.rotations[rot]->mirror;
        if (frame.rotations[rot]->hitMaskPath.has_value()) {
            std::snprintf(
                hitBuf, sizeof(hitBuf), "%s", frame.rotations[rot]->hitMaskPath->c_str());
        }
        if (frame.rotations[rot]->brightMapPath.has_value()) {
            std::snprintf(
                brightBuf, sizeof(brightBuf), "%s", frame.rotations[rot]->brightMapPath->c_str());
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
        textureBrowser.target = slopsprite::TextureBrowserTarget::Texture;
        textureBrowser.rescan(assets);
        textureBrowser.open = true;
    }

    if (mode == slopsprite::FrameRotationMode::Custom ||
        mode == slopsprite::FrameRotationMode::Eight) {
        labeledField("Mirror");
        if (ImGui::Checkbox("##mirror", &mirror)) {
            ensureRot().mirror = mirror;
            afterRotEdit();
        }
    }

    constexpr float kMaskThumbSize = 32.0f;
    auto drawMaskField = [&](const char* label,
                              const char* fieldId,
                              char* buf,
                              std::size_t bufSize,
                              const std::string& path,
                              slopsprite::TextureBrowserTarget target,
                              auto&& applyPath) {
        ImGui::PushID(fieldId);
        labeledField(label);
        const Texture2D thumb = path.empty() ? Texture2D{} : assets.getTexture(path);
        if (thumb.id != 0) {
            rlImGuiImageSize(&thumb, static_cast<int>(kMaskThumbSize), static_cast<int>(kMaskThumbSize));
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - iconButtonWidth() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##path", buf, bufSize)) {
            applyPath(std::string(buf));
        }
        ImGui::SameLine();
        const bool pick =
            slopengine::iconButton(assets, kIcons, "folder_page", ImVec2(iconButtonWidth(), 0.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pick texture");
        }
        if (pick) {
            textureBrowser.target = target;
            textureBrowser.rescan(assets);
            textureBrowser.open = true;
        }
        ImGui::PopID();
    };

    const std::string hitMaskPath =
        (frame.rotations[rot].has_value() && frame.rotations[rot]->hitMaskPath.has_value())
            ? *frame.rotations[rot]->hitMaskPath
            : std::string();
    drawMaskField(
        "Hit mask",
        "hitmask",
        hitBuf,
        sizeof(hitBuf),
        hitMaskPath,
        slopsprite::TextureBrowserTarget::HitMask,
        [&](std::string value) {
            slopengine::SpriteRotation& entry = ensureRot();
            if (value.empty()) {
                entry.hitMaskPath.reset();
            } else {
                entry.hitMaskPath = std::move(value);
            }
            afterRotEdit();
        });

    const std::string brightMaskPath =
        (frame.rotations[rot].has_value() && frame.rotations[rot]->brightMapPath.has_value())
            ? *frame.rotations[rot]->brightMapPath
            : std::string();
    drawMaskField(
        "Bright mask",
        "brightmask",
        brightBuf,
        sizeof(brightBuf),
        brightMaskPath,
        slopsprite::TextureBrowserTarget::BrightMask,
        [&](std::string value) {
            slopengine::SpriteRotation& entry = ensureRot();
            if (value.empty()) {
                entry.brightMapPath.reset();
            } else {
                entry.brightMapPath = std::move(value);
            }
            afterRotEdit();
        });

    std::string picked;
    if (textureBrowser.drawModal(assets, picked)) {
        switch (textureBrowser.target) {
            case slopsprite::TextureBrowserTarget::Texture: {
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
                break;
            }
            case slopsprite::TextureBrowserTarget::HitMask:
                ensureRot().hitMaskPath = picked;
                afterRotEdit();
                break;
            case slopsprite::TextureBrowserTarget::BrightMask:
                ensureRot().brightMapPath = picked;
                afterRotEdit();
                break;
        }
    }
}

void drawBaseTransformSection(slopsprite::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;

    if (editor.doc.selectedFrameIndex < 0 ||
        editor.doc.selectedFrameIndex >= static_cast<int>(editor.doc.asset.frames.size())) {
        ImGui::TextDisabled("Select a frame");
        return;
    }

    slopengine::SpriteFrame& frame =
        editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
    const slopsprite::FrameRotationMode mode = slopsprite::detectFrameRotationMode(frame);
    const int rot = slopsprite::clampSelectedRotToMode(mode, editor.doc.selectedRot);
    editor.doc.selectedRot = rot;

    if (!frame.rotations[rot].has_value()) {
        ImGui::TextDisabled("Selected rot has no texture — set it up above");
        return;
    }

    auto afterRotEdit = [&]() {
        if (mode == slopsprite::FrameRotationMode::Five) {
            slopsprite::syncFiveAngleMirrors(frame);
        }
        editor.markDirty();
        editor.doc.atlasDirty = true;
    };

    slopengine::SpriteRotation& entry = *frame.rotations[rot];
    if (!entry.hasOffset) {
        const int w = std::max(1, entry.pixelWidth);
        const int h = std::max(1, entry.pixelHeight);
        entry.offsetX = w / 2;
        entry.offsetY = h;
        entry.hasOffset = true;
    }
    int offset[2] = {entry.offsetX, entry.offsetY};
    labeledField("Offset");
    if (ImGui::DragInt2("##offset", offset, 1.0f)) {
        entry.offsetX = offset[0];
        entry.offsetY = offset[1];
        entry.hasOffset = true;
        afterRotEdit();
    }
    labeledField("Rotation");
    if (ImGui::DragFloat("##rotation", &entry.rotationDeg, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
        afterRotEdit();
    }
    float scale[2] = {entry.scaleX, entry.scaleY};
    labeledField("Scale");
    if (ImGui::DragFloat2("##scale", scale, 0.01f, 0.01f, 16.0f, "%.2f")) {
        entry.scaleX = scale[0];
        entry.scaleY = scale[1];
        afterRotEdit();
    }
    float translate[2] = {entry.translateX, entry.translateY};
    labeledField("Translate");
    if (ImGui::DragFloat2("##translate", translate, 0.5f, 0.0f, 0.0f, "%.1f")) {
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
    const slopsprite::FrameRotationMode mode = slopsprite::detectFrameRotationMode(frame);
    const int rot = slopsprite::clampSelectedRotToMode(mode, editor.doc.selectedRot);
    editor.doc.selectedRot = rot;
    const int authorCount = slopsprite::frameRotationAuthorCount(mode);

    if (!frame.rotations[rot].has_value()) {
        ImGui::TextDisabled("Selected rot has no texture — set it up above");
        return;
    }

    slopengine::SpriteRotation& entry = *frame.rotations[rot];
    auto afterAnimEdit = [&]() {
        if (mode == slopsprite::FrameRotationMode::Five) {
            slopsprite::syncFiveAngleMirrors(frame);
        }
        editor.markDirty();
    };

    labeledField("Anim rotation");
    if (ImGui::DragFloat("##animrotation", &entry.animRotationDeg, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
        afterAnimEdit();
    }
    float animScale[2] = {entry.animScaleX, entry.animScaleY};
    labeledField("Anim scale");
    if (ImGui::DragFloat2("##animscale", animScale, 0.01f, 0.01f, 16.0f, "%.2f")) {
        entry.animScaleX = animScale[0];
        entry.animScaleY = animScale[1];
        afterAnimEdit();
    }
    float animTranslate[2] = {entry.animTranslateX, entry.animTranslateY};
    labeledField("Anim translate");
    if (ImGui::DragFloat2("##animtranslate", animTranslate, 0.5f, 0.0f, 0.0f, "%.1f")) {
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

void drawOnionSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::SpritePicker& spritePicker) {
    constexpr const char* kIcons = kDefaultIconSet;

    labeledField("Compare");
    ImGui::Checkbox("##compare", &editor.doc.onionEnabled);
    if (editor.doc.onionEnabled) {
        const std::string onionLabel =
            editor.onionSpritePath.empty() ? "(this sprite)" : editor.onionSpritePath;
        ImGui::TextWrapped("Onion sprite: %s", onionLabel.c_str());
        if (buttonWithIcon(assets, kIcons, "folder_page", "Pick sprite", ImVec2(-1.0f, 0.0f))) {
            spritePicker.rescan(assets);
            spritePicker.open = true;
        }
        std::string pickedSprite;
        if (spritePicker.drawModal(assets, pickedSprite)) {
            editor.setOnionSprite(assets, pickedSprite);
        }
        if (!editor.onionSpritePath.empty() &&
            buttonWithIcon(assets, kIcons, "arrow_undo", "Use this sprite", ImVec2(-1.0f, 0.0f))) {
            editor.clearOnionSprite();
        }

        const slopengine::SpriteAsset& onionAsset = editor.onionAsset();
        if (!onionAsset.frames.empty()) {
            editor.doc.onionFrameIndex = std::clamp(
                editor.doc.onionFrameIndex,
                0,
                static_cast<int>(onionAsset.frames.size()) - 1);
            const char* onionPreview =
                onionAsset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)].id.c_str();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##onionframe", onionPreview)) {
                for (int i = 0; i < static_cast<int>(onionAsset.frames.size()); ++i) {
                    const bool selected = i == editor.doc.onionFrameIndex;
                    if (ImGui::Selectable(
                            onionAsset.frames[static_cast<std::size_t>(i)].id.c_str(), selected)) {
                        editor.doc.onionFrameIndex = i;
                    }
                }
                ImGui::EndCombo();
            }
            const slopsprite::FrameRotationMode onionMode = slopsprite::detectFrameRotationMode(
                onionAsset.frames[static_cast<std::size_t>(editor.doc.onionFrameIndex)]);
            editor.doc.onionRot = slopsprite::clampSelectedRotToMode(onionMode, editor.doc.onionRot);
            if (onionMode == slopsprite::FrameRotationMode::None) {
                ImGui::TextDisabled("Onion rot: 0");
            } else {
                labeledField("Onion rot");
                const int maxRot = onionMode == slopsprite::FrameRotationMode::Five ? 5 : 8;
                ImGui::SliderInt("##onionrot", &editor.doc.onionRot, 1, maxRot);
            }
        }
    }
    labeledField("Align zoom");
    ImGui::DragFloat("##alignzoom", &editor.doc.alignZoom, 0.05f, 0.25f, 16.0f, "%.2f");
}

void drawClipFramePropertiesSection(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::SoundBrowser& soundBrowser) {
    constexpr const char* kIcons = kDefaultIconSet;

    slopengine::SpriteAnimClip* clip = editor.currentAnimClip();
    if (clip == nullptr ||
        editor.doc.selectedClipFrameIndex < 0 ||
        editor.doc.selectedClipFrameIndex >= static_cast<int>(clip->frames.size())) {
        ImGui::TextDisabled("Select a clip frame in the Clips tab");
        return;
    }

    slopengine::SpriteAnimFrame& animFrame =
        clip->frames[static_cast<std::size_t>(editor.doc.selectedClipFrameIndex)];

    ImGui::TextDisabled("Tween out: R rot, S scale, T translate");
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

    ImGui::Separator();

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x - iconButtonWidth() - ImGui::GetStyle().ItemSpacing.x);
    char soundBuf[128] = {};
    std::snprintf(soundBuf, sizeof(soundBuf), "%s", animFrame.sound.c_str());
    if (ImGui::InputTextWithHint("##sound", "sound path", soundBuf, sizeof(soundBuf))) {
        animFrame.sound = soundBuf;
        editor.doc.animDirty = true;
    }
    ImGui::SameLine();
    const bool pickSound =
        slopengine::iconButton(assets, kIcons, "sound", ImVec2(iconButtonWidth(), 0.0f));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pick sound");
    }
    if (pickSound) {
        soundBrowser.rescan(assets);
        soundBrowser.filter.clear();
        soundBrowser.open = true;
    }
    if (animFrame.hasSound()) {
        labeledField("Volume");
        if (ImGui::DragFloat("##soundvol", &animFrame.soundVolume, 0.01f, 0.0f, 2.0f, "%.2f")) {
            editor.doc.animDirty = true;
        }
    }
    if (soundBrowser.open) {
        std::string picked;
        if (soundBrowser.drawModal(assets, picked)) {
            animFrame.sound = std::move(picked);
            editor.doc.animDirty = true;
        }
    }

    ImGui::Separator();

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

    ImGui::Separator();
    ImGui::TextDisabled("Overlays (layer sprite clip x y)");
    const int holdIndex = editor.doc.selectedClipFrameIndex;
    for (int oi = 0; oi < static_cast<int>(animFrame.overlays.size()); ++oi) {
        slopengine::SpriteAnimOverlay& overlay = animFrame.overlays[static_cast<std::size_t>(oi)];
        ImGui::PushID(oi + 1000);
        const bool selected = editor.doc.selectedOverlayHoldIndex == holdIndex &&
                              editor.doc.selectedOverlayIndex == oi;
        if (ImGui::SmallButton(selected ? "[*]" : "[ ]")) {
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
            editor.doc.selectedAttachPointIndex = -1;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(48.0f);
        if (ImGui::DragInt("##layer", &overlay.layer, 0.2f)) {
            if (overlay.layer == 0) {
                overlay.layer = 1;
            }
            editor.doc.animDirty = true;
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        char spriteBuf[128] = {};
        std::snprintf(spriteBuf, sizeof(spriteBuf), "%s", overlay.sprite.c_str());
        if (ImGui::InputTextWithHint("##osprite", "sprite", spriteBuf, sizeof(spriteBuf))) {
            overlay.sprite = spriteBuf;
            editor.doc.animDirty = true;
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        char clipBuf[64] = {};
        std::snprintf(clipBuf, sizeof(clipBuf), "%s", overlay.clip.c_str());
        if (ImGui::InputTextWithHint("##oclip", "clip", clipBuf, sizeof(clipBuf))) {
            overlay.clip = clipBuf;
            editor.doc.animDirty = true;
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        if (ImGui::DragFloat("##ox", &overlay.x, 0.5f, 0.0f, 0.0f, "x %.1f")) {
            editor.doc.animDirty = true;
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
        if (ImGui::DragFloat("##oy", &overlay.y, 0.5f, 0.0f, 0.0f, "y %.1f")) {
            editor.doc.animDirty = true;
            editor.doc.selectedOverlayHoldIndex = holdIndex;
            editor.doc.selectedOverlayIndex = oi;
        }
        ImGui::SameLine();
        const bool deleteOverlay = deleteIconButton(assets, kIcons);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete overlay");
        }
        if (deleteOverlay) {
            if (editor.doc.selectedOverlayHoldIndex == holdIndex &&
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
        editor.doc.selectedOverlayHoldIndex = holdIndex;
        editor.doc.selectedOverlayIndex = static_cast<int>(animFrame.overlays.size()) - 1;
        editor.doc.animDirty = true;
    }
}

void drawInspector(
    slopsprite::Editor& editor,
    slopengine::AssetStore& assets,
    slopsprite::TextureBrowser& textureBrowser,
    slopsprite::SpritePicker& onionSpritePicker,
    slopsprite::SoundBrowser& soundBrowser) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("Select a sprite");
        return;
    }

    ImGui::Text("Path: %s", editor.doc.virtualPath.c_str());
    ImGui::Text("Dirty: %s", editor.doc.dirty ? "yes" : "no");

    float texel = editor.doc.asset.pixelsPerMeter;
    labeledField("Texel size");
    if (ImGui::DragFloat("##texelsize", &texel, 0.5f, 1.0f, 512.0f, "%.1f")) {
        editor.doc.asset.pixelsPerMeter = texel;
        editor.markDirty();
    }

    labeledField("Fullbright");
    if (ImGui::Checkbox("##fullbright", &editor.doc.asset.fullbright)) {
        editor.markDirty();
    }

    {
        float tint[4] = {
            static_cast<float>(editor.doc.asset.tint.r) / 255.0f,
            static_cast<float>(editor.doc.asset.tint.g) / 255.0f,
            static_cast<float>(editor.doc.asset.tint.b) / 255.0f,
            static_cast<float>(editor.doc.asset.tint.a) / 255.0f,
        };
        labeledField("Tint");
        if (ImGui::ColorEdit4("##tint", tint, ImGuiColorEditFlags_AlphaBar)) {
            editor.doc.asset.tint = {
                static_cast<unsigned char>(std::clamp(tint[0], 0.0f, 1.0f) * 255.0f),
                static_cast<unsigned char>(std::clamp(tint[1], 0.0f, 1.0f) * 255.0f),
                static_cast<unsigned char>(std::clamp(tint[2], 0.0f, 1.0f) * 255.0f),
                static_cast<unsigned char>(std::clamp(tint[3], 0.0f, 1.0f) * 255.0f),
            };
            editor.markDirty();
        }
    }

    {
        const char* blendLabel =
            editor.doc.asset.blend == slopengine::SpriteBlendMode::Additive ? "Additive"
                                                                           : "Alpha";
        labeledField("Blend");
        if (ImGui::BeginCombo("##blend", blendLabel)) {
            if (ImGui::Selectable(
                    "Alpha",
                    editor.doc.asset.blend == slopengine::SpriteBlendMode::Alpha)) {
                editor.doc.asset.blend = slopengine::SpriteBlendMode::Alpha;
                editor.markDirty();
            }
            if (ImGui::Selectable(
                    "Additive",
                    editor.doc.asset.blend == slopengine::SpriteBlendMode::Additive)) {
                editor.doc.asset.blend = slopengine::SpriteBlendMode::Additive;
                editor.markDirty();
            }
            ImGui::EndCombo();
        }
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
        labeledField("Billboard");
        if (ImGui::BeginCombo("##billboard", modeLabel)) {
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

    if (ImGui::CollapsingHeader("Hit parts")) {
        constexpr const char* kIcons = kDefaultIconSet;
        static constexpr float kSuggestedHitPartColors[][3] = {
            {80 / 255.0f, 180 / 255.0f, 255 / 255.0f},
            {80 / 255.0f, 255 / 255.0f, 120 / 255.0f},
            {255 / 255.0f, 80 / 255.0f, 80 / 255.0f},
            {255 / 255.0f, 220 / 255.0f, 40 / 255.0f},
            {220 / 255.0f, 80 / 255.0f, 255 / 255.0f},
            {40 / 255.0f, 255 / 255.0f, 220 / 255.0f},
        };
        ImGui::TextDisabled("Colors in a frame's hit-mask image map to these named parts");
        for (int pi = 0; pi < static_cast<int>(editor.doc.asset.hitParts.size()); ++pi) {
            slopengine::SpriteHitPartDef& part =
                editor.doc.asset.hitParts[static_cast<std::size_t>(pi)];
            ImGui::PushID(pi + 4000);
            float color[3] = {
                static_cast<float>(part.r) / 255.0f,
                static_cast<float>(part.g) / 255.0f,
                static_cast<float>(part.b) / 255.0f,
            };
            if (ImGui::ColorEdit3(
                    "##hitpartcolor", color, ImGuiColorEditFlags_NoInputs)) {
                part.r = static_cast<unsigned char>(std::clamp(color[0], 0.0f, 1.0f) * 255.0f);
                part.g = static_cast<unsigned char>(std::clamp(color[1], 0.0f, 1.0f) * 255.0f);
                part.b = static_cast<unsigned char>(std::clamp(color[2], 0.0f, 1.0f) * 255.0f);
                editor.markDirty();
                editor.doc.atlasDirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x - iconButtonWidth() -
                ImGui::GetStyle().ItemSpacing.x);
            char nameBuf[64] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", part.name.c_str());
            if (ImGui::InputTextWithHint("##hitpartname", "name", nameBuf, sizeof(nameBuf))) {
                part.name = nameBuf;
                editor.markDirty();
                editor.doc.atlasDirty = true;
            }
            ImGui::SameLine();
            const bool deletePart = deleteIconButton(assets, kIcons);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Delete hit part");
            }
            if (deletePart) {
                editor.doc.asset.hitParts.erase(editor.doc.asset.hitParts.begin() + pi);
                editor.markDirty();
                editor.doc.atlasDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Add hit part")) {
            const std::size_t suggestCount =
                sizeof(kSuggestedHitPartColors) / sizeof(kSuggestedHitPartColors[0]);
            const float* suggested =
                kSuggestedHitPartColors[editor.doc.asset.hitParts.size() % suggestCount];
            slopengine::SpriteHitPartDef part{};
            part.name = "part";
            part.r = static_cast<unsigned char>(suggested[0] * 255.0f);
            part.g = static_cast<unsigned char>(suggested[1] * 255.0f);
            part.b = static_cast<unsigned char>(suggested[2] * 255.0f);
            editor.doc.asset.hitParts.push_back(part);
            editor.markDirty();
            editor.doc.atlasDirty = true;
        }
    }

    if (editor.mode == slopsprite::PreviewMode::FirstPerson &&
        ImGui::CollapsingHeader("View defaults", ImGuiTreeNodeFlags_DefaultOpen)) {
        float anchor[2] = {editor.doc.viewSprite.anchorX, editor.doc.viewSprite.anchorY};
        labeledField("Anchor");
        if (ImGui::DragFloat2("##anchor", anchor, 0.5f, 0.0f, 0.0f, "%.1f")) {
            editor.doc.viewSprite.anchorX = anchor[0];
            editor.doc.viewSprite.anchorY = anchor[1];
            editor.markDirty();
        }
        float origin[2] = {editor.doc.viewSprite.originX, editor.doc.viewSprite.originY};
        labeledField("Origin");
        if (ImGui::DragFloat2("##origin", origin, 0.01f, 0.0f, 1.0f, "%.2f")) {
            editor.doc.viewSprite.originX = origin[0];
            editor.doc.viewSprite.originY = origin[1];
            editor.markDirty();
        }
        float scale[2] = {editor.doc.viewSprite.scaleX, editor.doc.viewSprite.scaleY};
        labeledField("Scale");
        if (ImGui::DragFloat2("##viewscale", scale, 0.01f, 0.01f, 8.0f, "%.2f")) {
            editor.doc.viewSprite.scaleX = scale[0];
            editor.doc.viewSprite.scaleY = scale[1];
            editor.markDirty();
        }
        labeledField("Rotation");
        if (ImGui::DragFloat(
                "##viewrotation", &editor.doc.viewSprite.rotationDeg, 0.5f, -360.0f, 360.0f, "%.1f")) {
            editor.markDirty();
        }
    }

    if (editor.doc.selectedFrameIndex >= 0 &&
        editor.doc.selectedFrameIndex < static_cast<int>(editor.doc.asset.frames.size()) &&
        ImGui::CollapsingHeader("Attach points", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr const char* kIcons = kDefaultIconSet;
        slopengine::SpriteFrame& frame =
            editor.doc.asset.frames[static_cast<std::size_t>(editor.doc.selectedFrameIndex)];
        ImGui::TextDisabled("Named offsets on this frame (name x y z)");
        for (int pi = 0; pi < static_cast<int>(frame.attachPoints.size()); ++pi) {
            slopengine::SpriteAttachPoint& point =
                frame.attachPoints[static_cast<std::size_t>(pi)];
            ImGui::PushID(pi + 2000);
            const bool selected = editor.doc.selectedAttachPointIndex == pi;
            if (ImGui::SmallButton(selected ? "[*]" : "[ ]")) {
                editor.doc.selectedAttachPointIndex = selected ? -1 : pi;
                if (!selected) {
                    editor.doc.selectedOverlayHoldIndex = -1;
                    editor.doc.selectedOverlayIndex = -1;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
            char nameBuf[64] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", point.name.c_str());
            if (ImGui::InputTextWithHint("##apname", "name", nameBuf, sizeof(nameBuf))) {
                point.name = nameBuf;
                editor.markDirty();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            float xy[2] = {point.x, point.y};
            if (ImGui::DragFloat2("##apxy", xy, 0.5f, 0.0f, 0.0f, "%.1f")) {
                point.x = xy[0];
                point.y = xy[1];
                editor.markDirty();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x - iconButtonWidth() -
                ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::DragInt("##apz", &point.zIndex, 0.1f)) {
                editor.markDirty();
            }
            ImGui::SameLine();
            const bool deletePoint = deleteIconButton(assets, kIcons);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Delete attach point");
            }
            if (deletePoint) {
                if (editor.doc.selectedAttachPointIndex == pi) {
                    editor.doc.selectedAttachPointIndex = -1;
                }
                frame.attachPoints.erase(frame.attachPoints.begin() + pi);
                editor.markDirty();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Add attach point")) {
            slopengine::SpriteAttachPoint point{};
            point.name = "point";
            frame.attachPoints.push_back(point);
            editor.doc.selectedAttachPointIndex = static_cast<int>(frame.attachPoints.size()) - 1;
            editor.markDirty();
        }
        if (editor.mode == slopsprite::PreviewMode::FirstPerson) {
            ImGui::TextDisabled("Selected point draggable in FP preview");
        }
    }

    const bool isAlign = editor.mode == slopsprite::PreviewMode::Align;
    static bool sectionOpen[4] = {true, true, true, true};
    const int kSectionCount = 4;

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
        assets, kDefaultIconSet, "picture_edit", "Rotation", ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[0]) {
        if (ImGui::BeginChild("##rotation", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
            drawRotationSection(editor, assets, textureBrowser);
        }
        ImGui::EndChild();
    }

    sectionOpen[1] = collapsingHeaderWithIcon(
        assets, kDefaultIconSet, "shape_handles", "Base transforms", ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[1]) {
        if (ImGui::BeginChild("##basetrans", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
            drawBaseTransformSection(editor, assets);
        }
        ImGui::EndChild();
    }

    sectionOpen[2] = collapsingHeaderWithIcon(
        assets,
        kDefaultIconSet,
        "arrow_rotate_clockwise",
        "Anim transforms",
        ImGuiTreeNodeFlags_DefaultOpen);
    if (sectionOpen[2]) {
        if (ImGui::BeginChild("##animtrans", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
            drawAnimFrameSection(editor, assets);
        }
        ImGui::EndChild();
    }

    if (isAlign) {
        sectionOpen[3] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "layers", "Onion skin", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[3]) {
            if (ImGui::BeginChild("##onion", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
                drawOnionSection(editor, assets, onionSpritePicker);
            }
            ImGui::EndChild();
        }
    } else {
        sectionOpen[3] = collapsingHeaderWithIcon(
            assets, kDefaultIconSet, "film", "Clip frame", ImGuiTreeNodeFlags_DefaultOpen);
        if (sectionOpen[3]) {
            if (ImGui::BeginChild("##clipframe", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
                drawClipFramePropertiesSection(editor, assets, soundBrowser);
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

    slopengine::Log::init(config->mount.verbose ? slopengine::LogLevel::Debug : slopengine::LogLevel::Info);
    slopengine::Log::addDefaultConsoleSink();
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, "slopsprite");
    if (!IsWindowReady()) {
        std::cerr << "slopsprite: failed to initialize window\n";
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopsprite: --target must be one of the mounted packages\n";
        assets.releaseGpuResources();
        CloseWindow();
        return 1;
    }

    slopengine::setupImGuiWithUiFont(assets, slopengine::kDefaultUiFontPath, true, "slopsprite");

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
    slopsprite::SpritePicker onionSpritePicker;
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

        const float chromeHeight = slopengine::mainMenuBarHeight();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        const float viewTabHeight = ImGui::GetFrameHeightWithSpacing();
        const float animHeight = 128.0f;
        const slopsprite::UiLayout layout =
            slopsprite::computeUiLayout(chromeHeight, statusHeight, animHeight, viewTabHeight);
        slopsprite::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopsprite::pointInRect(mouse, layout.content);
        const bool allowPreviewInput =
            !uiWantsKeyboard && !uiWantsMouse && mouseInContent;

        auto setPreviewMode = [&](slopsprite::PreviewMode mode) { editor.mode = mode; };

        if (beginMainMenuBar()) {
            constexpr const char* kIcons = kDefaultIconSet;
            if (beginMenuWithIcon(assets, kIcons, "folder", "File", true)) {
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
            if (beginMenuWithIcon(assets, kIcons, "eye", "View", true)) {
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
                        "Sprite",
                        nullptr,
                        editor.mode == slopsprite::PreviewMode::Align)) {
                    setPreviewMode(slopsprite::PreviewMode::Align);
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "bug", "Debug", true)) {
                if (beginMenuWithIcon(assets, kIcons, "film", "Sprites")) {
                    menuItemWithIcon(
                        assets, kIcons, "color_swatch", "Masks", nullptr, &editor.showSpriteMasks);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            endMainMenuBar();
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

        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowPos(ImVec2(layout.viewTabBar.x, layout.viewTabBar.y));
        ImGui::SetNextWindowSize(ImVec2(layout.viewTabBar.width, layout.viewTabBar.height));
        ImGui::Begin(
            "##viewTabBar",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
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
            previewTabButton("Sprite", slopsprite::PreviewMode::Align);
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        ImGui::Begin("Browser", nullptr, panelFlags);
        browser.draw(editor, assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        ImGui::Begin("Inspector", nullptr, panelFlags);
        drawInspector(editor, assets, textureBrowser, onionSpritePicker, soundBrowser);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.animPanel.x, layout.animPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.animPanel.width, layout.animPanel.height));
        ImGui::Begin(
            "Animation",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        drawAnimBar(editor, assets);
        ImGui::End();

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
                    ImGui::Separator();
                    ImGui::TextUnformatted("Hit parts");
                    for (std::size_t i = 0; i < editor.doc.asset.hitParts.size(); ++i) {
                        const auto& part = editor.doc.asset.hitParts[i];
                        const ImVec4 color = ImVec4(
                            part.r / 255.0f, part.g / 255.0f, part.b / 255.0f, 1.0f);
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
                constexpr const char* kIcons = kDefaultIconSet;
                if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset")) {
                    editor.doc.fpZoom = 1.0f;
                    editor.doc.fpPanX = 0.0f;
                    editor.doc.fpPanY = 0.0f;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("FP  RMB pan  Wheel zoom");
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
                constexpr const char* kIcons = kDefaultIconSet;
                if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset")) {
                    editor.doc.alignZoom = 2.0f;
                    editor.doc.alignPanX = 0.0f;
                    editor.doc.alignPanY = 0.0f;
                }
                ImGui::SameLine();
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
                        "Sprite  RMB pan  wheel zoom  rot %d  off %d,%d",
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
                modeLabel = "Sprite";
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
    slopengine::unloadSpriteAtlas(editor.onionRefAtlas);
    audioWorld.deinit();
    assets.releaseGpuResources();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
