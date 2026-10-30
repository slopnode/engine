#include "browser.hpp"
#include "editor.hpp"
#include "fp_preview.hpp"
#include "layout.hpp"
#include "model_draw.hpp"
#include "world_preview.hpp"

#include "assets/asset_store.hpp"
#include "core/log.hpp"
#include "core/package_meta.hpp"
#include "core/package_search.hpp"
#include "core/user_paths.hpp"
#include "game/app_config.hpp"
#include "ui/icon_ui.hpp"
#include "ui/slopui_tool.hpp"
#include "ui/slopui_widgets.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    std::string openGeo;
    std::string openClip;
    float openFrame = -1.0f;
    bool twoSided = false;
    std::vector<std::pair<std::string, std::string>> previews;
    slopmodel::PreviewMode mode = slopmodel::PreviewMode::World;
};

void printUsage() {
    std::cerr
        << "Usage: slopmodel --game <path> [--mod <path>]... [--target <path>] [--geo <path>]\n"
        << "\n"
        << "  --game        Game package directory (required)\n"
        << "  --mod         Additional mod package directory (repeatable)\n"
        << "  --target      Package directory that receives saves (defaults\n"
        << "                to --game; required when --mod is given)\n"
        << "  --geo         Geometry virtual path to open at start\n"
        << "  --mode        Preview to start in: world (default) or fp\n"
        << "  --clip        Clip to play at start, with --geo\n"
        << "  --frame       Pause the clip at this frame instead of playing it\n"
        << "  --preview     attachment=geo, show a geo on an attachment (repeatable)\n"
        << "  --two-sided   Draw both faces of every triangle\n"
        << "  --verbose     Raise the trace log level to show DEBUG messages\n";
}

std::optional<ToolConfig> parseArgs(int argc, char* argv[]) {
    ToolConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&]() -> const char* {
            if (i + 1 >= argc) {
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--game") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.game = value;
            continue;
        }
        if (arg == "--mod") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.mount.mods.emplace_back(value);
            continue;
        }
        if (arg == "--target") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.target = slopengine::resolveApplicationPackagePath(
                value,
                slopengine::applicationSearchPaths(slopengine::userConfiguredSearchPaths()));
            continue;
        }
        if (arg == "--geo") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.openGeo = value;
            continue;
        }
        if (arg == "--clip") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.openClip = value;
            continue;
        }
        if (arg == "--frame") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            config.openFrame = static_cast<float>(std::atof(value));
            continue;
        }
        if (arg == "--preview") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            const std::string spec = value;
            const auto eq = spec.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
                return std::nullopt;
            }
            config.previews.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
            continue;
        }
        if (arg == "--two-sided") {
            config.twoSided = true;
            continue;
        }
        if (arg == "--mode") {
            const char* value = needValue();
            if (value == nullptr) {
                return std::nullopt;
            }
            if (std::strcmp(value, "fp") == 0) {
                config.mode = slopmodel::PreviewMode::FirstPerson;
            } else if (std::strcmp(value, "world") == 0) {
                config.mode = slopmodel::PreviewMode::World;
            } else {
                return std::nullopt;
            }
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

constexpr float kLabelColumnWidth = 96.0f;

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

bool vector3Field(const char* label, Vector3& value, float speed, const char* format = "%.3f") {
    labeledField(label);
    float values[3] = {value.x, value.y, value.z};
    std::string id = "##";
    id += label;
    if (ImGui::DragFloat3(id.c_str(), values, speed, 0.0f, 0.0f, format)) {
        value = {values[0], values[1], values[2]};
        return true;
    }
    return false;
}

bool boneCombo(
    const char* id,
    const slopengine::SkeletonAsset* skeleton,
    std::string& bone) {
    bool changed = false;
    labeledField("Bone");
    if (ImGui::BeginCombo(id, bone.empty() ? "(none)" : bone.c_str())) {
        if (skeleton != nullptr) {
            for (const slopengine::SkeletonBone& candidate : skeleton->bones) {
                const bool selected = candidate.name == bone;
                if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                    bone = candidate.name;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawModelSection(slopmodel::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;
    if (!collapsingHeaderWithIcon(assets, kIcons, "package", "Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const slopmodel::Document& doc = editor.doc;
    ImGui::Text("geo: %s", doc.virtualPath.c_str());
    ImGui::Text("package: %s", doc.packageId.c_str());
    if (doc.skeletonPath.empty()) {
        ImGui::TextDisabled("static geo, no skeleton");
    } else {
        ImGui::Text("skeleton: %s (%d bones)", doc.skeletonPath.c_str(), doc.model.skeleton.boneCount);
        if (doc.hasAnim) {
            ImGui::Text("clips: %d", static_cast<int>(doc.anim.clips.size()));
        } else {
            ImGui::TextDisabled("no animation bank at animations/%s.anim", doc.skeletonPath.c_str());
        }
    }
    ImGui::TextUnformatted("primitives");
    for (const slopengine::GeoPrimitive& primitive : doc.geo.primitives) {
        ImGui::BulletText("%s  %s", primitive.name.c_str(), primitive.material.c_str());
    }
    labeledField("World scale");
    if (ImGui::DragFloat("##worldscale", &editor.doc.worldScale, 0.01f, 0.01f, 20.0f, "%.2f")) {
        editor.doc.worldScale = std::clamp(editor.doc.worldScale, 0.01f, 20.0f);
    }
}

void drawViewSection(slopmodel::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;
    if (!collapsingHeaderWithIcon(assets, kIcons, "eye", "View placement", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    slopengine::GeoView& view = editor.doc.geo.view;
    if (ImGui::Checkbox("Store view defaults", &view.present)) {
        editor.markGeoDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("fp-attach-geo uses these when the script passes no placement");
    }
    ImGui::BeginDisabled(!view.present);
    if (vector3Field("Position", view.position, 0.005f)) {
        editor.markGeoDirty();
    }
    if (vector3Field("Rotation", view.rotationDeg, 0.5f, "%.1f")) {
        editor.markGeoDirty();
    }
    if (vector3Field("Scale", view.scale, 0.005f)) {
        editor.markGeoDirty();
    }
    if (buttonWithIcon(assets, kIcons, "arrow_undo", "Reset")) {
        view.position = {0.0f, 0.0f, 0.0f};
        view.rotationDeg = {0.0f, 0.0f, 0.0f};
        view.scale = {1.0f, 1.0f, 1.0f};
        editor.markGeoDirty();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("(fp-attach-geo \"weapon\" \"%s\")", editor.doc.virtualPath.c_str());
}

void drawAttachmentsSection(
    slopmodel::Editor& editor,
    slopengine::AssetStore& assets,
    slopmodel::GeoPicker& picker,
    int& pickerTarget) {
    constexpr const char* kIcons = kDefaultIconSet;
    if (!collapsingHeaderWithIcon(assets, kIcons, "shape_handles", "Attachments", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!editor.skinned()) {
        ImGui::TextDisabled("Attachments need a skinned geo");
        return;
    }
    const slopengine::SkeletonAsset* skeleton = assets.getSkeletonAsset(editor.doc.skeletonPath);

    if (buttonWithIcon(assets, kIcons, "add", "Add attachment")) {
        editor.addAttachment();
    }

    std::vector<slopengine::GeoAttachment>& attachments = editor.doc.geo.attachments;
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(attachments.size()); ++i) {
        slopengine::GeoAttachment& attachment = attachments[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool selected = i == editor.doc.selectedAttachment;
        const float nameWidth = ImGui::GetContentRegionAvail().x - iconButtonWidth() -
            ImGui::GetStyle().ItemSpacing.x;
        const std::string rowLabel = attachment.name + "  (" + attachment.bone + ")";
        if (ImGui::Selectable(rowLabel.c_str(), selected, 0, ImVec2(std::max(20.0f, nameWidth), 0.0f))) {
            editor.doc.selectedAttachment = i;
        }
        ImGui::SameLine();
        if (deleteIconButton(assets, kIcons)) {
            removeIndex = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delete attachment");
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        editor.removeAttachment(removeIndex);
    }

    const int selectedIndex = editor.doc.selectedAttachment;
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(attachments.size())) {
        return;
    }
    slopengine::GeoAttachment& attachment = attachments[static_cast<std::size_t>(selectedIndex)];
    ImGui::Separator();
    ImGui::PushID("selectedattachment");
    char nameBuf[64] = {};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", attachment.name.c_str());
    labeledField("Name");
    if (ImGui::InputText("##attname", nameBuf, sizeof(nameBuf))) {
        attachment.name = nameBuf;
        editor.markGeoDirty();
    }
    if (boneCombo("##attbone", skeleton, attachment.bone)) {
        editor.markGeoDirty();
    }
    if (vector3Field("Position", attachment.position, 0.002f)) {
        editor.markGeoDirty();
    }
    if (vector3Field("Rotation", attachment.rotationDeg, 0.5f, "%.1f")) {
        editor.markGeoDirty();
    }
    labeledField("Scale");
    if (ImGui::DragFloat("##attscale", &attachment.scale, 0.005f, 0.001f, 100.0f, "%.3f")) {
        editor.markGeoDirty();
    }

    slopmodel::AttachmentPreview& preview = editor.doc.previews[static_cast<std::size_t>(selectedIndex)];
    labeledField("Preview geo");
    const float pickWidth = ImGui::GetContentRegionAvail().x - iconButtonWidth() * 2.0f -
        ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(20.0f, pickWidth));
    ImGui::BeginDisabled();
    char previewBuf[256] = {};
    std::snprintf(previewBuf, sizeof(previewBuf), "%s", preview.geoPath.c_str());
    ImGui::InputText("##previewgeo", previewBuf, sizeof(previewBuf));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (slopengine::iconButton(assets, kIcons, "folder_find", ImVec2(iconButtonWidth(), 0.0f))) {
        picker.rescan(assets);
        picker.open = true;
        pickerTarget = selectedIndex;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pick a geo to show on this attachment (preview only)");
    }
    ImGui::SameLine();
    if (deleteIconButton(assets, kIcons)) {
        editor.clearAttachmentPreview(selectedIndex);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear preview geo");
    }
    ImGui::TextDisabled("(actor-attach-geo id \"held\" \"%s\" geo)", attachment.name.c_str());
    ImGui::PopID();
}

void drawEventsSection(slopmodel::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;
    if (!collapsingHeaderWithIcon(assets, kIcons, "lightning", "Clip events", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    slopengine::AnimClip* clip = editor.currentClipMeta();
    if (clip == nullptr) {
        ImGui::TextDisabled("Pick a clip in the animation bar");
        return;
    }
    const slopmodel::ClipInfo info = editor.clipInfo(assets);
    const int currentFrame = info.valid ? static_cast<int>(std::lround(editor.doc.time * info.fps)) : 0;
    char addLabel[64];
    std::snprintf(addLabel, sizeof(addLabel), "Add event at frame %d", currentFrame);
    if (buttonWithIcon(assets, kIcons, "add", addLabel)) {
        editor.addEvent(currentFrame, "event");
    }
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(clip->events.size()); ++i) {
        slopengine::AnimEvent& event = clip->events[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(64.0f);
        int frame = event.frame;
        if (ImGui::InputInt("##frame", &frame, 0, 0)) {
            event.frame = std::clamp(frame, 0, std::max(0, info.frameCount - 1));
            editor.markAnimDirty();
        }
        ImGui::SameLine();
        char nameBuf[64] = {};
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", event.name.c_str());
        const float nameWidth = ImGui::GetContentRegionAvail().x - iconButtonWidth() -
            ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(20.0f, nameWidth));
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
            event.name = nameBuf;
            editor.markAnimDirty();
        }
        ImGui::SameLine();
        if (deleteIconButton(assets, kIcons)) {
            removeIndex = i;
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        editor.removeEvent(removeIndex);
    }
    ImGui::TextDisabled("(on-anim-event id event clip)");
}

void drawInspector(
    slopmodel::Editor& editor,
    slopengine::AssetStore& assets,
    slopmodel::GeoPicker& picker,
    int& pickerTarget) {
    if (!editor.doc.open) {
        ImGui::TextDisabled("Open a model from the browser");
        return;
    }
    if (slopengine::slopuiBeginChild("##inspectorscroll", ImVec2(0.0f, 0.0f), 0)) {
        drawModelSection(editor, assets);
        drawViewSection(editor, assets);
        drawAttachmentsSection(editor, assets, picker, pickerTarget);
        drawEventsSection(editor, assets);
    }
    ImGui::EndChild();
}

void drawAnimBar(slopmodel::Editor& editor, slopengine::AssetStore& assets) {
    constexpr const char* kIcons = kDefaultIconSet;
    if (!editor.doc.open) {
        ImGui::TextDisabled("No model open");
        return;
    }
    if (!editor.doc.hasAnim || editor.doc.anim.clips.empty()) {
        ImGui::TextDisabled("No animation bank for this skeleton");
        return;
    }

    ImGui::TextUnformatted("Clip");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    const char* preview = editor.doc.clip.empty() ? "(none)" : editor.doc.clip.c_str();
    if (ImGui::BeginCombo("##clip", preview)) {
        for (const slopengine::AnimClip& clip : editor.doc.anim.clips) {
            const bool selected = clip.name == editor.doc.clip;
            if (ImGui::Selectable(clip.name.c_str(), selected)) {
                editor.playClip(clip.name, editor.doc.loop);
                editor.scrub(assets, 0.0f);
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
            editor.doc.playing ? "control_pause" : "control_play",
            editor.doc.playing ? "Pause" : "Play")) {
        if (editor.doc.playing) {
            editor.stop();
        } else {
            editor.doc.playing = true;
            editor.doc.lastFrame = -1.0f;
        }
    }
    ImGui::SameLine();
    if (buttonWithIcon(assets, kIcons, "control_stop", "Stop")) {
        editor.scrub(assets, 0.0f);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &editor.doc.loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("Speed", &editor.doc.speed, 0.01f, 0.01f, 8.0f, "%.2f");

    const slopmodel::ClipInfo info = editor.clipInfo(assets);
    if (!info.valid) {
        ImGui::TextDisabled("Clip has no tracks");
        return;
    }
    const float lastFrame = static_cast<float>(std::max(info.frameCount - 1, 1));
    float frame = editor.doc.time * info.fps;
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("##scrub", &frame, 0.0f, lastFrame, "frame %.1f")) {
        editor.scrub(assets, frame / info.fps);
    }
    ImGui::SameLine();
    ImGui::Text("%.0f fps  %.2fs%s", info.fps, info.duration, editor.doc.animDirty ? "  *" : "");
    if (editor.doc.lastEventTimer > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "event: %s", editor.doc.lastEvent.c_str());
    }

    const ImVec2 trackMin = ImGui::GetCursorScreenPos();
    const float trackWidth = ImGui::GetContentRegionAvail().x - 160.0f;
    const float trackHeight = 18.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        trackMin,
        ImVec2(trackMin.x + trackWidth, trackMin.y + trackHeight),
        IM_COL32(40, 42, 50, 255));
    if (const slopengine::AnimClip* clip = editor.currentClipMeta()) {
        for (const slopengine::AnimEvent& event : clip->events) {
            const float t = lastFrame > 0.0f ? static_cast<float>(event.frame) / lastFrame : 0.0f;
            const float x = trackMin.x + std::clamp(t, 0.0f, 1.0f) * trackWidth;
            drawList->AddLine(ImVec2(x, trackMin.y), ImVec2(x, trackMin.y + trackHeight), IM_COL32(255, 200, 60, 255), 2.0f);
            const float labelWidth = ImGui::CalcTextSize(event.name.c_str()).x;
            const float labelX = std::min(x + 3.0f, trackMin.x + trackWidth - labelWidth - 2.0f);
            drawList->AddText(ImVec2(labelX, trackMin.y + 2.0f), IM_COL32(255, 220, 120, 255), event.name.c_str());
        }
    }
    const float cursorX = trackMin.x + std::clamp(frame / lastFrame, 0.0f, 1.0f) * trackWidth;
    drawList->AddLine(ImVec2(cursorX, trackMin.y), ImVec2(cursorX, trackMin.y + trackHeight), IM_COL32(120, 200, 255, 255), 1.0f);
    ImGui::Dummy(ImVec2(trackWidth, trackHeight));
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
    InitWindow(1600, 900, "slopmodel");
    if (!IsWindowReady()) {
        std::cerr << "slopmodel: failed to initialize window\n";
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    slopengine::AssetStore assets(config->mount);
    if (!targetIsMounted(assets, config->target)) {
        std::cerr << "slopmodel: --target must be one of the mounted packages\n";
        assets.releaseGpuResources();
        CloseWindow();
        return 1;
    }

    slopengine::installToolTheme(assets, "slopmodel");

    slopmodel::Editor editor;
    editor.targetRoot = config->target;
    editor.mode = config->mode;
    editor.twoSided = config->twoSided;
    if (auto meta = slopengine::loadPackageMetaFile(config->target / "package.meta")) {
        editor.targetPackageId = meta->id;
    }

    slopmodel::ModelBrowser browser;
    browser.rescan(assets);
    slopmodel::GeoPicker picker;
    int pickerTarget = -1;
    slopmodel::WorldPreview worldPreview;
    slopmodel::FpPreview fpPreview;
    slopmodel::ShadedDraw shaded;
    shaded.init(assets);
    RenderTexture2D contentTarget{};
    bool quit = false;

    if (!config->openGeo.empty() && editor.load(assets, config->openGeo)) {
        if (!config->openClip.empty()) {
            editor.playClip(config->openClip, true);
            if (config->openFrame >= 0.0f) {
                const slopmodel::ClipInfo info = editor.clipInfo(assets);
                editor.scrub(assets, info.valid ? config->openFrame / info.fps : 0.0f);
            }
        }
        for (const auto& [attachmentName, geoPath] : config->previews) {
            for (int i = 0; i < static_cast<int>(editor.doc.geo.attachments.size()); ++i) {
                if (editor.doc.geo.attachments[static_cast<std::size_t>(i)].name == attachmentName) {
                    editor.setAttachmentPreview(assets, i, geoPath);
                }
            }
        }
    }

    while (!quit) {
        if (WindowShouldClose()) {
            quit = true;
        }

        editor.tick(GetFrameTime(), assets);
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
        const float animHeight = 96.0f;
        const slopmodel::UiLayout layout =
            slopmodel::computeUiLayout(chromeHeight, statusHeight, animHeight, viewTabHeight);
        slopmodel::ensureContentTarget(contentTarget, layout.content);

        const Vector2 mouse = GetMousePosition();
        const bool mouseInContent = slopmodel::pointInRect(mouse, layout.content);
        const bool allowPreviewInput = !uiWantsKeyboard && !uiWantsMouse && mouseInContent;

        if (beginMainMenuBar()) {
            constexpr const char* kIcons = kDefaultIconSet;
            if (beginMenuWithIcon(assets, kIcons, "folder", "File", true)) {
                if (menuItemWithIcon(assets, kIcons, "disk", "Save", "Ctrl+S", false, editor.doc.open)) {
                    editor.save(assets);
                }
                if (menuItemWithIcon(assets, kIcons, "arrow_refresh", "Rescan Models")) {
                    browser.rescan(assets);
                }
                ImGui::Separator();
                if (menuItemWithIcon(assets, kIcons, "door", "Quit")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (beginMenuWithIcon(assets, kIcons, "eye", "View", true)) {
                if (menuItemWithIcon(assets, kIcons, "world", "World", nullptr, editor.mode == slopmodel::PreviewMode::World)) {
                    editor.mode = slopmodel::PreviewMode::World;
                }
                if (menuItemWithIcon(assets, kIcons, "user", "First Person", nullptr, editor.mode == slopmodel::PreviewMode::FirstPerson)) {
                    editor.mode = slopmodel::PreviewMode::FirstPerson;
                }
                ImGui::Separator();
                menuItemWithIcon(assets, kIcons, "shape_handles", "Skeleton", nullptr, &editor.showSkeleton);
                menuItemWithIcon(assets, kIcons, "tag_blue", "Attachment markers", nullptr, &editor.showAttachments);
                menuItemWithIcon(assets, kIcons, "palette", "Shaded", nullptr, &editor.shaded);
                menuItemWithIcon(assets, kIcons, "layers", "Two-sided", nullptr, &editor.twoSided);
                ImGui::EndMenu();
            }
            endMainMenuBar();
        }

        if (!uiWantsKeyboard) {
            if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S) && editor.doc.open) {
                editor.save(assets);
            }
            if (IsKeyPressed(KEY_SPACE) && editor.doc.open && editor.doc.hasAnim) {
                if (editor.doc.playing) {
                    editor.stop();
                } else {
                    editor.doc.playing = true;
                    editor.doc.lastFrame = -1.0f;
                }
            }
        }

        const ImGuiWindowFlags panelFlags =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowPos(ImVec2(layout.viewTabBar.x, layout.viewTabBar.y));
        ImGui::SetNextWindowSize(ImVec2(layout.viewTabBar.width, layout.viewTabBar.height));
        slopengine::slopuiBegin(
            "##viewTabBar",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        if (slopengine::slopuiBeginTabBar("##previewTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            auto previewTabButton = [&](const char* label, slopmodel::PreviewMode mode) {
                const bool selected = editor.mode == mode;
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                }
                if (slopengine::slopuiTabItemButton(label)) {
                    editor.mode = mode;
                }
                if (selected) {
                    ImGui::PopStyleColor(2);
                }
            };
            previewTabButton("World", slopmodel::PreviewMode::World);
            previewTabButton("FP", slopmodel::PreviewMode::FirstPerson);
            slopengine::slopuiEndTabBar();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.leftPanel.x, layout.leftPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.leftPanel.width, layout.leftPanel.height));
        slopengine::slopuiBegin("Browser", nullptr, panelFlags);
        browser.draw(editor, assets);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.rightPanel.x, layout.rightPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.rightPanel.width, layout.rightPanel.height));
        slopengine::slopuiBegin("Inspector", nullptr, panelFlags);
        drawInspector(editor, assets, picker, pickerTarget);
        {
            std::string picked;
            if (picker.drawModal(assets, picked)) {
                editor.setAttachmentPreview(assets, pickerTarget, picked);
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(layout.animPanel.x, layout.animPanel.y));
        ImGui::SetNextWindowSize(ImVec2(layout.animPanel.width, layout.animPanel.height));
        slopengine::slopuiBegin(
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

        ImGui::SetNextWindowPos(ImVec2(layout.content.x + 8.0f, layout.content.y + 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        if (editor.mode == slopmodel::PreviewMode::World) {
            if (slopengine::slopuiBegin("##worldoverlay", nullptr, overlayFlags)) {
                constexpr const char* kIcons = kDefaultIconSet;
                if (buttonWithIcon(
                        assets,
                        kIcons,
                        worldPreview.autoOrbit ? "control_pause" : "arrow_rotate_clockwise",
                        worldPreview.autoOrbit ? "Stop" : "Spin")) {
                    worldPreview.autoOrbit = !worldPreview.autoOrbit;
                }
                ImGui::SameLine();
                if (buttonWithIcon(assets, kIcons, "arrow_inout", "Frame")) {
                    editor.requestWorldCameraFrame = true;
                }
                ImGui::TextDisabled("RMB orbit  Wheel zoom  LMB pick attachment");
            }
            ImGui::End();
            worldPreview.draw(editor, shaded, contentTarget, layout.content, allowPreviewInput);
        } else {
            if (slopengine::slopuiBegin("##fpoverlay", nullptr, overlayFlags)) {
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("fov", &fpPreview.fovy, 0.5f, 30.0f, 120.0f, "%.0f");
                ImGui::TextDisabled("View socket as the engine draws it");
                if (editor.doc.open && !editor.doc.geo.view.present) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No view defaults stored yet");
                }
            }
            ImGui::End();
            fpPreview.draw(editor, shaded, contentTarget, layout.content, allowPreviewInput);
        }

        {
            std::string statusLine = "target: ";
            statusLine += editor.targetPackageId.empty() ? editor.targetRoot.string() : editor.targetPackageId;
            statusLine += " | mode: ";
            statusLine += editor.mode == slopmodel::PreviewMode::World ? "World" : "FP";
            if (editor.doc.open) {
                statusLine += " | ";
                statusLine += editor.doc.virtualPath;
                if (editor.doc.geoDirty || editor.doc.animDirty) {
                    statusLine += "*";
                }
            }
            if (!editor.statusMessage.empty()) {
                statusLine += " | ";
                statusLine += editor.statusMessage;
            }
            ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(GetScreenHeight()) - statusHeight), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GetScreenWidth()), statusHeight), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            if (slopengine::slopuiBegin(
                    "##status",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
                ImGui::TextUnformatted(statusLine.c_str());
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }

        BeginDrawing();
        ClearBackground(Color{24, 24, 28, 255});
        slopmodel::drawContentTarget(contentTarget, layout.content);
        rlImGuiEnd();
        EndDrawing();
    }

    if (contentTarget.id != 0) {
        UnloadRenderTexture(contentTarget);
    }
    editor.close();
    shaded.release();
    assets.releaseGpuResources();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
