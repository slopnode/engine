#include "editor.hpp"

#include "assets/geo_loader.hpp"
#include "assets/skeleton_loader.hpp"
#include "render/bone_attachment.hpp"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>

namespace slopmodel {

namespace {

std::string parentOf(const std::string& virtualPath) {
    const auto slash = virtualPath.find_last_of('/');
    return slash == std::string::npos ? std::string{} : virtualPath.substr(0, slash);
}

std::string stemOf(const std::string& fileName) {
    const auto slash = fileName.find_last_of('/');
    const std::string base = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

bool writeText(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Failed to create " + path.parent_path().string();
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Failed to open " + path.string();
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        error = "Failed writing " + path.string();
        return false;
    }
    return true;
}

/** Copies the binary sibling of a text asset into the target package when the target lacks it. */
void copySiblingIntoTarget(
    slopengine::AssetStore& assets,
    slopengine::AssetKind kind,
    const std::string& virtualPath,
    const std::filesystem::path& destination) {
    std::error_code ec;
    if (std::filesystem::exists(destination, ec)) {
        return;
    }
    const auto source = assets.resolvePath(kind, virtualPath);
    if (!source) {
        return;
    }
    std::filesystem::create_directories(destination.parent_path(), ec);
    std::filesystem::copy_file(*source, destination, ec);
}

} // namespace

Matrix transformMatrix(const Transform& transform) {
    return MatrixMultiply(
        MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
        MatrixMultiply(
            QuaternionToMatrix(transform.rotation),
            MatrixTranslate(
                transform.translation.x, transform.translation.y, transform.translation.z)));
}

Matrix viewPlacementMatrix(const slopengine::GeoView& view) {
    return transformMatrix(slopengine::geoViewTransform(view));
}

void Editor::setStatus(std::string message, float seconds) {
    statusMessage = std::move(message);
    statusTimer = seconds;
}

void Editor::markGeoDirty() {
    doc.geoDirty = true;
}

void Editor::markAnimDirty() {
    doc.animDirty = true;
}

bool Editor::skinned() const {
    return doc.open && doc.model.skeleton.boneCount > 0 && doc.model.skeleton.bones != nullptr;
}

bool Editor::load(slopengine::AssetStore& assets, const std::string& virtualPath) {
    close();

    const slopengine::GeoAsset* parsed = assets.getGeoAsset(virtualPath);
    if (parsed == nullptr) {
        setStatus("Failed to parse geo " + virtualPath);
        return false;
    }
    const Model source = assets.getGeoModel(virtualPath);
    if (source.meshCount <= 0) {
        setStatus("Failed to load geo " + virtualPath);
        return false;
    }

    doc.model = slopengine::cloneGeoModelInstance(source);
    doc.geo = *parsed;
    doc.virtualPath = virtualPath;
    if (const auto owned = assets.resolveOwned(slopengine::AssetKind::Geo, virtualPath);
        owned && owned->package != nullptr) {
        doc.packageId = owned->package->meta().id;
    }
    doc.skeletonPath = assets.resolveGeoSkeletonPath(virtualPath);
    if (!doc.skeletonPath.empty() && assets.hasAnim(doc.skeletonPath)) {
        doc.hasAnim = slopengine::parseAnimAsset(assets.getAnimSource(doc.skeletonPath), doc.anim);
    }
    doc.previews.resize(doc.geo.attachments.size());
    doc.selectedAttachment = doc.geo.attachments.empty() ? -1 : 0;
    doc.open = true;

    if (doc.hasAnim && !doc.anim.clips.empty()) {
        playClip(doc.anim.clips.front().name, true);
        applyPose(assets);
    }
    requestWorldCameraFrame = true;
    setStatus("Opened " + virtualPath);
    return true;
}

void Editor::close() {
    if (!doc.open) {
        doc = {};
        return;
    }
    for (AttachmentPreview& preview : doc.previews) {
        if (preview.model.meshCount > 0) {
            slopengine::unloadClonedGeoModelInstance(preview.model);
        }
    }
    slopengine::unloadClonedGeoModelInstance(doc.model);
    doc = {};
}

bool Editor::save(slopengine::AssetStore& assets) {
    if (!doc.open || doc.virtualPath.empty()) {
        setStatus("Nothing to save");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    std::string error;
    const std::filesystem::path geoOut = targetRoot / "geometry" / (doc.virtualPath + ".geo");
    if (!writeText(geoOut, slopengine::serializeGeoAsset(doc.geo), error)) {
        setStatus(error);
        return false;
    }
    copySiblingIntoTarget(
        assets,
        slopengine::AssetKind::GeoVert,
        doc.virtualPath,
        targetRoot / "geometry" / (doc.virtualPath + ".vert"));
    if (doc.geo.weightsImplicit) {
        copySiblingIntoTarget(
            assets,
            slopengine::AssetKind::GeoWeights,
            doc.virtualPath,
            targetRoot / "geometry" / (doc.virtualPath + ".weights"));
    }
    doc.geoDirty = false;
    assets.invalidateGeo(doc.virtualPath);

    if (doc.hasAnim && doc.animDirty) {
        const std::filesystem::path animOut =
            targetRoot / "animations" / (doc.skeletonPath + ".anim");
        if (!writeText(animOut, slopengine::serializeAnimAsset(doc.anim), error)) {
            setStatus(error);
            return false;
        }
        const std::string bankDir = parentOf(doc.skeletonPath);
        for (const slopengine::AnimClip& clip : doc.anim.clips) {
            const std::string tracksStem =
                clip.tracksImplicit ? stemOf(doc.skeletonPath) : stemOf(clip.tracksFile);
            const std::string tracksVirtual =
                bankDir.empty() ? tracksStem : bankDir + "/" + tracksStem;
            copySiblingIntoTarget(
                assets,
                slopengine::AssetKind::AnimTracks,
                tracksVirtual,
                targetRoot / "animations" / (tracksVirtual + ".tracks"));
        }
        doc.animDirty = false;
        assets.invalidateAnim(doc.skeletonPath);
    }

    setStatus("Saved " + doc.virtualPath);
    return true;
}

const slopengine::AnimBank* Editor::bank(slopengine::AssetStore& assets) const {
    if (!doc.hasAnim || doc.skeletonPath.empty()) {
        return nullptr;
    }
    return assets.getAnimBank(doc.skeletonPath);
}

ClipInfo Editor::clipInfo(slopengine::AssetStore& assets) const {
    ClipInfo info{};
    const slopengine::AnimBank* clips = bank(assets);
    if (clips == nullptr) {
        return info;
    }
    const auto it = clips->clipIndexByName.find(doc.clip);
    if (it == clips->clipIndexByName.end() || it->second >= clips->clips.size()) {
        return info;
    }
    const slopengine::AnimClip& meta = clips->clipMeta[it->second];
    const ModelAnimation& clip = clips->clips[it->second];
    if (clip.keyframeCount <= 0) {
        return info;
    }
    info.fps = meta.fps > 0.0f ? meta.fps : 30.0f;
    info.frameCount = clip.keyframeCount;
    info.duration = meta.duration;
    if (info.duration <= 0.0f) {
        const int lastFrame = clip.keyframeCount - 1;
        info.duration = static_cast<float>(lastFrame > 0 ? lastFrame : 1) / info.fps;
    }
    info.valid = true;
    return info;
}

slopengine::AnimClip* Editor::currentClipMeta() {
    if (!doc.hasAnim) {
        return nullptr;
    }
    for (slopengine::AnimClip& clip : doc.anim.clips) {
        if (clip.name == doc.clip) {
            return &clip;
        }
    }
    return nullptr;
}

void Editor::playClip(const std::string& name, bool loop) {
    doc.clip = name;
    doc.loop = loop;
    doc.time = 0.0f;
    doc.playing = true;
    doc.lastFrame = -1.0f;
}

void Editor::stop() {
    doc.playing = false;
}

void Editor::scrub(slopengine::AssetStore& assets, float time) {
    const ClipInfo info = clipInfo(assets);
    doc.playing = false;
    doc.time = info.valid ? std::clamp(time, 0.0f, info.duration) : 0.0f;
    doc.lastFrame = doc.time * info.fps;
    applyPose(assets);
}

void Editor::tick(float dt, slopengine::AssetStore& assets) {
    if (doc.lastEventTimer > 0.0f) {
        doc.lastEventTimer -= dt;
    }
    if (!doc.playing) {
        return;
    }
    const ClipInfo info = clipInfo(assets);
    if (!info.valid) {
        doc.playing = false;
        return;
    }
    doc.time += dt * doc.speed;
    bool wrapped = false;
    if (doc.time >= info.duration) {
        if (doc.loop) {
            doc.time = std::fmod(doc.time, info.duration);
            wrapped = true;
        } else {
            doc.time = info.duration;
            doc.playing = false;
        }
    }
    applyPose(assets);

    const float frame = doc.time * info.fps;
    if (const slopengine::AnimClip* meta = currentClipMeta(); meta != nullptr && !meta->events.empty()) {
        std::vector<const slopengine::AnimEvent*> fired;
        slopengine::collectAnimEvents(*meta, doc.lastFrame, frame, wrapped, fired);
        if (!fired.empty()) {
            doc.lastEvent = fired.back()->name;
            doc.lastEventTimer = 0.8f;
        }
    }
    doc.lastFrame = frame;
}

void Editor::applyPose(slopengine::AssetStore& assets) {
    if (!skinned()) {
        return;
    }
    const slopengine::AnimBank* clips = bank(assets);
    if (clips == nullptr) {
        return;
    }
    const auto it = clips->clipIndexByName.find(doc.clip);
    if (it == clips->clipIndexByName.end() || it->second >= clips->clips.size()) {
        return;
    }
    const std::size_t index = it->second;
    const ClipInfo info = clipInfo(assets);
    if (!info.valid) {
        return;
    }
    const std::vector<Matrix>* bind = assets.getSkeletonBindMatrices(doc.skeletonPath);
    const slopengine::AnimClipMatrices* matrixClip =
        index < clips->matrixClips.size() ? &clips->matrixClips[index] : nullptr;
    slopengine::updateRiggedModelAnimation(
        doc.model, clips->clips[index], doc.time * info.fps, bind, matrixClip);
}

int Editor::addAttachment() {
    slopengine::GeoAttachment attachment{};
    attachment.name = "attach" + std::to_string(doc.geo.attachments.size());
    if (skinned()) {
        attachment.bone = doc.model.skeleton.bones[0].name;
    }
    doc.geo.attachments.push_back(std::move(attachment));
    doc.previews.emplace_back();
    doc.selectedAttachment = static_cast<int>(doc.geo.attachments.size()) - 1;
    markGeoDirty();
    return doc.selectedAttachment;
}

void Editor::removeAttachment(int index) {
    if (index < 0 || index >= static_cast<int>(doc.geo.attachments.size())) {
        return;
    }
    clearAttachmentPreview(index);
    doc.geo.attachments.erase(doc.geo.attachments.begin() + index);
    doc.previews.erase(doc.previews.begin() + index);
    if (doc.selectedAttachment >= static_cast<int>(doc.geo.attachments.size())) {
        doc.selectedAttachment = static_cast<int>(doc.geo.attachments.size()) - 1;
    }
    markGeoDirty();
}

bool Editor::setAttachmentPreview(
    slopengine::AssetStore& assets,
    int index,
    const std::string& geoPath) {
    if (index < 0 || index >= static_cast<int>(doc.previews.size())) {
        return false;
    }
    clearAttachmentPreview(index);
    if (geoPath.empty() || !assets.hasGeo(geoPath)) {
        return false;
    }
    const Model source = assets.getGeoModel(geoPath);
    if (source.meshCount <= 0) {
        setStatus("Failed to load geo " + geoPath);
        return false;
    }
    AttachmentPreview& preview = doc.previews[static_cast<std::size_t>(index)];
    preview.geoPath = geoPath;
    preview.model = slopengine::cloneGeoModelInstance(source);
    return true;
}

void Editor::clearAttachmentPreview(int index) {
    if (index < 0 || index >= static_cast<int>(doc.previews.size())) {
        return;
    }
    AttachmentPreview& preview = doc.previews[static_cast<std::size_t>(index)];
    if (preview.model.meshCount > 0) {
        slopengine::unloadClonedGeoModelInstance(preview.model);
    }
    preview = {};
}

Transform Editor::attachmentPose(int index) const {
    Transform identity{};
    identity.rotation = QuaternionIdentity();
    identity.scale = {1.0f, 1.0f, 1.0f};
    if (index < 0 || index >= static_cast<int>(doc.geo.attachments.size()) || !skinned()) {
        return identity;
    }
    const slopengine::GeoAttachment& attachment =
        doc.geo.attachments[static_cast<std::size_t>(index)];
    const int bone = slopengine::findModelBoneIndex(doc.model, attachment.bone);
    if (bone < 0) {
        return identity;
    }
    const Transform* poses =
        doc.model.currentPose != nullptr ? doc.model.currentPose : doc.model.skeleton.bindPose;
    if (poses == nullptr) {
        return identity;
    }
    const Transform local = slopengine::geoAttachmentTransform(attachment);
    slopengine::BoneAttachment offset{};
    offset.offset = local.translation;
    offset.rotation = local.rotation;
    offset.scale = local.scale;
    return slopengine::composeBoneAttachment(poses[bone], offset);
}

int Editor::addEvent(int frame, const std::string& name) {
    slopengine::AnimClip* clip = currentClipMeta();
    if (clip == nullptr) {
        return -1;
    }
    slopengine::AnimEvent event{};
    event.frame = std::max(0, frame);
    event.name = name.empty() ? "event" : name;
    clip->events.push_back(std::move(event));
    markAnimDirty();
    return static_cast<int>(clip->events.size()) - 1;
}

void Editor::removeEvent(int index) {
    slopengine::AnimClip* clip = currentClipMeta();
    if (clip == nullptr || index < 0 || index >= static_cast<int>(clip->events.size())) {
        return;
    }
    clip->events.erase(clip->events.begin() + index);
    markAnimDirty();
}

}
