#include "browser.hpp"

#include "ui/icon_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace slopsprite {

namespace {

bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h(haystack.size(), '\0');
    std::string n(needle.size(), '\0');
    std::transform(haystack.begin(), haystack.end(), h.begin(), lower);
    std::transform(needle.begin(), needle.end(), n.begin(), lower);
    return h.find(n) != std::string::npos;
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

void drawFramesSection(Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;

    if (!editor.doc.open) {
        ImGui::TextDisabled("No sprite open");
        return;
    }

    if (slopengine::buttonWithIcon(assets, kIcons, "image_add", "Add empty")) {
        slopengine::SpriteFrame frame{};
        frame.id = "F" + std::to_string(editor.doc.asset.frames.size());
        editor.doc.asset.frames.push_back(std::move(frame));
        editor.selectFrameIndex(static_cast<int>(editor.doc.asset.frames.size()) - 1);
        editor.markDirty();
        editor.doc.atlasDirty = true;
    }
    ImGui::SameLine();
    if (slopengine::buttonWithIcon(assets, kIcons, "page_copy", "Duplicate selected")) {
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
            slopengine::drawIconImGui(assets, kIcons, "images", 16.0f);
            ImGui::SetCursorScreenPos(
                ImVec2(pickPos.x + pickSize + ImGui::GetStyle().ItemSpacing.x, pickPos.y));
        }
        ImGui::SameLine(0.0f, 0.0f);
        char idBuf[64] = {};
        std::snprintf(idBuf, sizeof(idBuf), "%s", frame.id.c_str());
        const float idWidth = ImGui::GetContentRegionAvail().x - iconButtonWidth() -
            ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(20.0f, idWidth));
        if (ImGui::InputText("##frameid", idBuf, sizeof(idBuf))) {
            editor.renameFrame(i, idBuf);
        }
        ImGui::SameLine();
        const bool deleteFrame = deleteIconButton(assets, kIcons);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete frame");
        }
        if (deleteFrame && editor.doc.asset.frames.size() > 1) {
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

void drawClipFramesSection(Editor& editor, slopengine::AssetStore& assets, SoundBrowser& soundBrowser) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;

    if (!editor.doc.open) {
        ImGui::TextDisabled("No sprite open");
        return;
    }

    if (!editor.doc.hasAnim || editor.doc.animBank.clips.empty()) {
        ImGui::TextDisabled("No .spanim for this sprite");
        if (slopengine::buttonWithIcon(
                assets, kIcons, "page_add", "Create .spanim", ImVec2(-1.0f, 0.0f))) {
            editor.ensureAnimBank();
        }
        return;
    }

    {
        const char* preview =
            editor.doc.animClip.empty() ? "(none)" : editor.doc.animClip.c_str();
        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - iconButtonWidth() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##clipselect", preview)) {
            for (const slopengine::SpriteAnimClip& c : editor.doc.animBank.clips) {
                const bool selected = c.name == editor.doc.animClip;
                if (ImGui::Selectable(c.name.c_str(), selected)) {
                    editor.doc.animLoop = c.loop;
                    editor.playAnimClip(c.name, c.loop);
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
        const bool deleteClip = deleteIconButton(assets, kIcons);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete clip");
        }
        if (deleteClip && !editor.doc.animClip.empty()) {
            editor.deleteClip(editor.doc.animClip);
        }

        static char newClipNameBuf[64] = {};
        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - iconButtonWidth() - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##newclipname", "new clip name", newClipNameBuf, sizeof(newClipNameBuf));
        ImGui::SameLine();
        const bool newClip =
            slopengine::iconButton(assets, kIcons, "film_add", ImVec2(iconButtonWidth(), 0.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("New clip");
        }
        if (newClip) {
            editor.addClip(newClipNameBuf);
            newClipNameBuf[0] = '\0';
        }
    }
    ImGui::Separator();

    slopengine::SpriteAnimClip* clip = editor.currentAnimClip();
    if (clip == nullptr) {
        ImGui::TextDisabled("No clip selected");
        return;
    }

    if (slopengine::buttonWithIcon(
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
        const bool moveFrameUp =
            slopengine::iconButton(assets, kIcons, "arrow_up", ImVec2(iconButtonWidth(), 0.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move up");
        }
        if (moveFrameUp && i > 0) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i - 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        const bool moveFrameDown =
            slopengine::iconButton(assets, kIcons, "arrow_down", ImVec2(iconButtonWidth(), 0.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move down");
        }
        if (moveFrameDown && i + 1 < static_cast<int>(clip->frames.size())) {
            std::swap(
                clip->frames[static_cast<std::size_t>(i)],
                clip->frames[static_cast<std::size_t>(i + 1)]);
            editor.doc.animDirty = true;
        }
        ImGui::SameLine();
        const bool deleteClipFrame = deleteIconButton(assets, kIcons);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete frame");
        }
        if (deleteClipFrame && clip->frames.size() > 1) {
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
        const bool pickSound =
            slopengine::iconButton(assets, kIcons, "sound", ImVec2(iconButtonWidth(), 0.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pick sound");
        }
        if (pickSound) {
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
            const bool deleteOverlay = deleteIconButton(assets, kIcons);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Delete overlay");
            }
            if (deleteOverlay) {
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

} // namespace

void SpriteBrowser::rescan(const slopengine::AssetStore& assets) {
    std::unordered_map<std::string, std::string> owningPackage;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path spritesRoot = package.root() / "sprites";
        if (!std::filesystem::exists(spritesRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(spritesRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".spr") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative =
                std::filesystem::relative(it->path(), spritesRoot, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            const std::string virtualPath = relative.generic_string();
            owningPackage[virtualPath] = package.meta().id;
        }
    }

    entries.clear();
    entries.reserve(owningPackage.size());
    for (const auto& [path, packageId] : owningPackage) {
        entries.push_back(SpriteBrowserEntry{path, packageId});
    }
    std::sort(entries.begin(), entries.end(), [](const SpriteBrowserEntry& a, const SpriteBrowserEntry& b) {
        return a.virtualPath < b.virtualPath;
    });
}

void SpriteBrowser::draw(Editor& editor, slopengine::AssetStore& assets, SoundBrowser& soundBrowser) {
    constexpr const char* kIcons = slopengine::kDefaultIconSet;

    ImGui::TextUnformatted("Sprites");
    ImGui::SetNextItemWidth(-1.0f);
    char filterBuf[128] = {};
    std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
    if (ImGui::InputText("##spritefilter", filterBuf, sizeof(filterBuf))) {
        filter = filterBuf;
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "page_add", "New", ImVec2(-1.0f, 0.0f))) {
        editor.showNewSpriteModal = true;
    }
    if (slopengine::buttonWithIcon(assets, kIcons, "arrow_refresh", "Rescan", ImVec2(-1.0f, 0.0f))) {
        rescan(assets);
    }
    ImGui::Separator();

    const float listHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);

    if (ImGui::BeginChild("##spritebrowserlist", ImVec2(0.0f, listHeight), false)) {
        for (const SpriteBrowserEntry& entry : entries) {
            if (!containsIgnoreCase(entry.virtualPath, filter) &&
                !containsIgnoreCase(entry.packageId, filter)) {
                continue;
            }
            const bool selected = editor.doc.open && editor.doc.virtualPath == entry.virtualPath;
            ImGui::PushID(entry.virtualPath.c_str());
            if (slopengine::selectableWithIcon(
                    assets, kIcons, "images", entry.virtualPath.c_str(), selected)) {
                if (!editor.doc.dirty || selected) {
                    editor.loadSprite(assets, entry.virtualPath);
                } else {
                    editor.loadSprite(assets, entry.virtualPath);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\npackage: %s", entry.virtualPath.c_str(), entry.packageId.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    if (ImGui::BeginChild("##sprframestabs", ImVec2(0.0f, 0.0f), false)) {
        if (ImGui::BeginTabBar("##browserTabs")) {
            if (ImGui::BeginTabItem("Frames")) {
                drawFramesSection(editor, assets);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Clips")) {
                drawClipFramesSection(editor, assets, soundBrowser);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
}

void SpritePicker::rescan(const slopengine::AssetStore& assets) {
    std::unordered_set<std::string> seen;
    for (const slopengine::Package& package : assets.packages()) {
        const std::filesystem::path spritesRoot = package.root() / "sprites";
        if (!std::filesystem::exists(spritesRoot)) {
            continue;
        }
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(spritesRoot, ec), end;
             it != end && !ec;
             it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() != ".spr") {
                continue;
            }
            std::error_code relEc;
            std::filesystem::path relative =
                std::filesystem::relative(it->path(), spritesRoot, relEc);
            if (relEc) {
                continue;
            }
            relative.replace_extension();
            seen.insert(relative.generic_string());
        }
    }

    sprites.assign(seen.begin(), seen.end());
    std::sort(sprites.begin(), sprites.end());
}

bool SpritePicker::drawModal(slopengine::AssetStore& assets, std::string& outPath) {
    if (!open) {
        return false;
    }

    constexpr const char* kIcons = slopengine::kDefaultIconSet;
    bool picked = false;
    ImGui::OpenPopup("Pick Sprite");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Pick Sprite", &open, ImGuiWindowFlags_None)) {
        ImGui::SetNextItemWidth(-1.0f);
        char filterBuf[128] = {};
        std::snprintf(filterBuf, sizeof(filterBuf), "%s", filter.c_str());
        if (ImGui::InputText("##spritepickfilter", filterBuf, sizeof(filterBuf))) {
            filter = filterBuf;
        }
        ImGui::Separator();
        if (ImGui::BeginChild(
                "##spritepicklist", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true)) {
            for (const std::string& path : sprites) {
                if (!containsIgnoreCase(path, filter)) {
                    continue;
                }
                if (slopengine::selectableWithIcon(assets, kIcons, "images", path.c_str(), false)) {
                    outPath = path;
                    picked = true;
                    open = false;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
        }
        ImGui::EndChild();
        if (slopengine::buttonWithIcon(assets, kIcons, "cancel", "Cancel", ImVec2(-1.0f, 0.0f))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return picked;
}

}
