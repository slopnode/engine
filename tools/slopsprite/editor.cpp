#include "editor.hpp"

#include "audio/audio_world.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>

namespace slopsprite {

namespace {

constexpr int kFiveAnglePairSrc[] = {2, 3, 4};
constexpr int kFiveAnglePairDst[] = {8, 7, 6};

bool rotationFilled(const slopengine::SpriteFrame& frame, int rot) {
    return frame.rotations[rot].has_value() && !frame.rotations[rot]->texturePath.empty();
}

bool isFiveAngleMirrorPair(const slopengine::SpriteFrame& frame, int src, int dst) {
    if (!frame.rotations[src].has_value() || !frame.rotations[dst].has_value()) {
        return false;
    }
    const slopengine::SpriteRotation& a = *frame.rotations[src];
    const slopengine::SpriteRotation& b = *frame.rotations[dst];
    return a.texturePath == b.texturePath && b.mirror;
}

slopengine::SpriteRotation mirroredCopy(const slopengine::SpriteRotation& src) {
    slopengine::SpriteRotation dst = src;
    dst.mirror = true;
    if (dst.hasOffset && dst.pixelWidth > 0) {
        dst.offsetX = dst.pixelWidth - 1 - dst.offsetX;
    }
    return dst;
}

void fillPixelSizes(slopengine::SpriteAsset& asset, const slopengine::SpriteAtlas& atlas) {
    for (slopengine::SpriteFrame& frame : asset.frames) {
        for (int rotation = 0; rotation < slopengine::kSpriteRotationCount; ++rotation) {
            if (!frame.rotations[rotation].has_value()) {
                continue;
            }
            slopengine::SpriteRotation& entry = *frame.rotations[rotation];
            const auto rectIt = atlas.rects.find(entry.texturePath);
            if (rectIt == atlas.rects.end()) {
                continue;
            }
            entry.pixelWidth = static_cast<int>(rectIt->second.source.width);
            entry.pixelHeight = static_cast<int>(rectIt->second.source.height);
        }
    }
}

float computeClipDuration(const slopengine::SpriteAnimClip& clip) {
    float duration = 0.0f;
    for (const slopengine::SpriteAnimFrame& frame : clip.frames) {
        duration += frame.duration;
    }
    return duration;
}

const slopengine::SpriteAnimClip* findClip(
    const slopengine::SpriteAnimBank& bank,
    const std::string& name) {
    const auto it = bank.clipIndexByName.find(name);
    if (it == bank.clipIndexByName.end() || it->second >= bank.clips.size()) {
        return nullptr;
    }
    return &bank.clips[it->second];
}

void clearAnimTween(EditorDocument& doc) {
    doc.animTweenRotation = false;
    doc.animTweenScale = false;
    doc.animTweenTranslate = false;
    doc.animTransformBlend = 0.0f;
    doc.animNextFrame.clear();
}

void firePreviewSound(
    EditorDocument& doc,
    const slopengine::SpriteAnimClip& clip,
    int previousIndex,
    int currentIndex,
    slopengine::AssetStore& assets,
    slopengine::AudioWorld* audio) {
    if (audio == nullptr || !audio->ready() || currentIndex < 0 ||
        currentIndex >= static_cast<int>(clip.frames.size())) {
        return;
    }
    auto playHold = [&](int index) {
        const slopengine::SpriteAnimFrame& frame = clip.frames[static_cast<std::size_t>(index)];
        if (!frame.hasSound()) {
            return;
        }
        audio->playSound(assets, frame.sound, frame.soundVolume);
    };
    if (previousIndex < 0) {
        playHold(currentIndex);
        return;
    }
    if (previousIndex == currentIndex) {
        return;
    }
    const int frameCount = static_cast<int>(clip.frames.size());
    if (doc.animLoop && currentIndex < previousIndex) {
        for (int i = previousIndex + 1; i < frameCount; ++i) {
            playHold(i);
        }
        for (int i = 0; i <= currentIndex; ++i) {
            playHold(i);
        }
        return;
    }
    const int begin = previousIndex + 1;
    const int end = std::min(currentIndex, frameCount - 1);
    for (int i = begin; i <= end; ++i) {
        playHold(i);
    }
}

void applyAnimTime(
    EditorDocument& doc,
    const slopengine::SpriteAnimClip& clip,
    float time,
    bool loop) {
    clearAnimTween(doc);
    doc.animHoldIndex = -1;
    if (clip.frames.empty()) {
        return;
    }
    float clipDuration = computeClipDuration(clip);
    if (clipDuration <= 0.0f) {
        doc.currentFrame = clip.frames.front().id;
        doc.animHoldIndex = 0;
        return;
    }

    float localTime = time;
    if (loop) {
        while (localTime >= clipDuration) {
            localTime -= clipDuration;
        }
        while (localTime < 0.0f) {
            localTime += clipDuration;
        }
    } else if (localTime >= clipDuration) {
        doc.currentFrame = clip.frames.back().id;
        doc.animHoldIndex = static_cast<int>(clip.frames.size()) - 1;
        for (std::size_t i = 0; i < doc.asset.frames.size(); ++i) {
            if (doc.asset.frames[i].id == doc.currentFrame) {
                doc.selectedFrameIndex = static_cast<int>(i);
                break;
            }
        }
        return;
    }

    float cursor = 0.0f;
    for (std::size_t frameIndex = 0; frameIndex < clip.frames.size(); ++frameIndex) {
        const slopengine::SpriteAnimFrame& frame = clip.frames[frameIndex];
        const float next = cursor + frame.duration;
        if (localTime <= next || frameIndex + 1 == clip.frames.size()) {
            doc.currentFrame = frame.id;
            doc.animHoldIndex = static_cast<int>(frameIndex);
            for (std::size_t i = 0; i < doc.asset.frames.size(); ++i) {
                if (doc.asset.frames[i].id == doc.currentFrame) {
                    doc.selectedFrameIndex = static_cast<int>(i);
                    break;
                }
            }
            if (frame.hasTween() && frame.duration > 0.0f) {
                std::size_t nextIndex = frameIndex + 1;
                if (nextIndex >= clip.frames.size()) {
                    if (!loop) {
                        return;
                    }
                    nextIndex = 0;
                }
                const float holdTime = localTime - cursor;
                doc.animTweenRotation = frame.tweenRotation;
                doc.animTweenScale = frame.tweenScale;
                doc.animTweenTranslate = frame.tweenTranslate;
                doc.animTransformBlend = holdTime / frame.duration;
                doc.animNextFrame = clip.frames[nextIndex].id;
            }
            return;
        }
        cursor = next;
    }
}

bool parseViewCanvasFromSource(std::string_view source, int& width, int& height) {
    const std::size_t namePos = source.find("*view-canvas*");
    if (namePos == std::string_view::npos) {
        return false;
    }
    const std::size_t open = source.find('\'', namePos);
    if (open == std::string_view::npos) {
        return false;
    }
    const std::size_t paren = source.find('(', open);
    if (paren == std::string_view::npos) {
        return false;
    }
    const std::size_t close = source.find(')', paren);
    if (close == std::string_view::npos) {
        return false;
    }
    const std::string_view pair = source.substr(paren + 1, close - paren - 1);
    int w = 0;
    int h = 0;
    const auto* begin = pair.data();
    const auto* end = pair.data() + pair.size();
    auto result = std::from_chars(begin, end, w);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    begin = result.ptr;
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    result = std::from_chars(begin, end, h);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    width = w;
    height = h;
    return true;
}

} // namespace

FrameRotationMode detectFrameRotationMode(const slopengine::SpriteFrame& frame) {
    bool anyDirectional = false;
    for (int rot = 1; rot < slopengine::kSpriteRotationCount; ++rot) {
        if (rotationFilled(frame, rot)) {
            anyDirectional = true;
            break;
        }
    }
    if (!anyDirectional) {
        return FrameRotationMode::None;
    }

    bool allDirectional = true;
    for (int rot = 1; rot < slopengine::kSpriteRotationCount; ++rot) {
        if (!frame.rotations[rot].has_value()) {
            allDirectional = false;
            break;
        }
    }
    if (!allDirectional) {
        return FrameRotationMode::Custom;
    }

    bool fiveStyle = true;
    for (int i = 0; i < 3; ++i) {
        if (!isFiveAngleMirrorPair(frame, kFiveAnglePairSrc[i], kFiveAnglePairDst[i])) {
            fiveStyle = false;
            break;
        }
    }
    if (fiveStyle && !frame.rotations[1]->mirror && !frame.rotations[5]->mirror) {
        return FrameRotationMode::Five;
    }
    return FrameRotationMode::Eight;
}

void syncFiveAngleMirrors(slopengine::SpriteFrame& frame) {
    for (int i = 0; i < 3; ++i) {
        const int src = kFiveAnglePairSrc[i];
        const int dst = kFiveAnglePairDst[i];
        if (!frame.rotations[src].has_value()) {
            frame.rotations[dst].reset();
            continue;
        }
        frame.rotations[dst] = mirroredCopy(*frame.rotations[src]);
    }
}

void applyFrameRotationMode(slopengine::SpriteFrame& frame, FrameRotationMode mode) {
    if (mode == FrameRotationMode::Custom) {
        return;
    }

    if (mode == FrameRotationMode::None) {
        slopengine::SpriteRotation keep{};
        if (frame.rotations[0].has_value()) {
            keep = *frame.rotations[0];
        } else {
            for (int rot = 1; rot < slopengine::kSpriteRotationCount; ++rot) {
                if (frame.rotations[rot].has_value()) {
                    keep = *frame.rotations[rot];
                    keep.mirror = false;
                    break;
                }
            }
        }
        for (int rot = 0; rot < slopengine::kSpriteRotationCount; ++rot) {
            frame.rotations[rot].reset();
        }
        frame.rotations[0] = keep;
        return;
    }

    if (!frame.rotations[1].has_value()) {
        if (frame.rotations[0].has_value()) {
            frame.rotations[1] = *frame.rotations[0];
            frame.rotations[1]->mirror = false;
        } else {
            frame.rotations[1] = slopengine::SpriteRotation{};
        }
    }
    frame.rotations[0].reset();

    for (int rot = 2; rot < slopengine::kSpriteRotationCount; ++rot) {
        if (!frame.rotations[rot].has_value()) {
            frame.rotations[rot] = slopengine::SpriteRotation{};
        }
    }

    if (mode == FrameRotationMode::Five) {
        frame.rotations[1]->mirror = false;
        if (frame.rotations[5].has_value()) {
            frame.rotations[5]->mirror = false;
        }
        syncFiveAngleMirrors(frame);
        return;
    }

    for (int rot = 1; rot < slopengine::kSpriteRotationCount; ++rot) {
        if (frame.rotations[rot].has_value()) {
            frame.rotations[rot]->mirror = false;
        }
    }
}

int frameRotationAuthorCount(FrameRotationMode mode) {
    switch (mode) {
    case FrameRotationMode::None:
        return 1;
    case FrameRotationMode::Five:
        return 5;
    case FrameRotationMode::Eight:
        return 8;
    case FrameRotationMode::Custom:
        return slopengine::kSpriteRotationCount;
    }
    return 1;
}

int frameRotationAuthorIndex(FrameRotationMode mode, int slot) {
    switch (mode) {
    case FrameRotationMode::None:
        return 0;
    case FrameRotationMode::Five:
        return slot + 1;
    case FrameRotationMode::Eight:
        return slot + 1;
    case FrameRotationMode::Custom:
        return slot;
    }
    return 0;
}

int clampSelectedRotToMode(FrameRotationMode mode, int selectedRot) {
    switch (mode) {
    case FrameRotationMode::None:
        return 0;
    case FrameRotationMode::Five:
        if (selectedRot >= 1 && selectedRot <= 5) {
            return selectedRot;
        }
        return 1;
    case FrameRotationMode::Eight:
        if (selectedRot >= 1 && selectedRot <= 8) {
            return selectedRot;
        }
        return 1;
    case FrameRotationMode::Custom:
        return std::clamp(selectedRot, 0, slopengine::kSpriteRotationCount - 1);
    }
    return 0;
}

void Editor::setStatus(std::string message, float seconds) {
    statusMessage = std::move(message);
    statusTimer = seconds;
}

void Editor::applyViewFromAsset() {
    if (!doc.asset.view.present) {
        doc.viewSprite = {};
        doc.eyeOffsetX = 0.0f;
        doc.eyeOffsetY = 0.0f;
        doc.eyeOffsetZ = 0.0f;
        return;
    }
    doc.viewSprite.canvasX = doc.asset.view.canvasX;
    doc.viewSprite.canvasY = doc.asset.view.canvasY;
    doc.viewSprite.scaleX = doc.asset.view.scaleX;
    doc.viewSprite.scaleY = doc.asset.view.scaleY;
    doc.viewSprite.rotationDeg = doc.asset.view.rotationDeg;
    doc.viewSprite.originX = doc.asset.view.originX;
    doc.viewSprite.originY = doc.asset.view.originY;
    doc.eyeOffsetX = doc.asset.view.eyeOffsetX;
    doc.eyeOffsetY = doc.asset.view.eyeOffsetY;
    doc.eyeOffsetZ = doc.asset.view.eyeOffsetZ;
}

void Editor::syncViewToAsset() {
    doc.asset.view.present = true;
    doc.asset.view.canvasX = doc.viewSprite.canvasX;
    doc.asset.view.canvasY = doc.viewSprite.canvasY;
    doc.asset.view.scaleX = doc.viewSprite.scaleX;
    doc.asset.view.scaleY = doc.viewSprite.scaleY;
    doc.asset.view.rotationDeg = doc.viewSprite.rotationDeg;
    doc.asset.view.originX = doc.viewSprite.originX;
    doc.asset.view.originY = doc.viewSprite.originY;
    doc.asset.view.eyeOffsetX = doc.eyeOffsetX;
    doc.asset.view.eyeOffsetY = doc.eyeOffsetY;
    doc.asset.view.eyeOffsetZ = doc.eyeOffsetZ;
}

void Editor::markDirty() {
    doc.dirty = true;
}

void Editor::rebuildAtlas(slopengine::AssetStore& assets) {
    slopengine::unloadSpriteAtlas(doc.atlas);
    doc.atlas = slopengine::buildSpriteAtlas(doc.asset, [&assets](std::string_view texturePath) {
        return assets.resolvePath(slopengine::AssetKind::Texture, texturePath);
    });
    fillPixelSizes(doc.asset, doc.atlas);
    doc.atlasDirty = false;
}

bool Editor::loadSprite(slopengine::AssetStore& assets, const std::string& virtualPath) {
    if (!assets.hasSprite(virtualPath)) {
        setStatus("Sprite not found: " + virtualPath);
        return false;
    }

    slopengine::SpriteAsset asset{};
    if (!slopengine::parseSpriteAsset(assets.getSpriteSource(virtualPath), asset)) {
        setStatus("Failed to parse sprite: " + virtualPath);
        return false;
    }

    slopengine::unloadSpriteAtlas(doc.atlas);
    doc = EditorDocument{};
    doc.virtualPath = virtualPath;
    doc.asset = std::move(asset);
    doc.open = true;
    doc.atlasDirty = true;
    if (!doc.asset.frames.empty()) {
        doc.currentFrame = doc.asset.frames.front().id;
        doc.selectedFrameIndex = 0;
    }
    applyViewFromAsset();

    if (assets.hasSpriteAnim(virtualPath)) {
        if (slopengine::parseSpriteAnimBank(assets.getSpriteAnimSource(virtualPath), doc.animBank)) {
            doc.hasAnim = true;
            if (!doc.animBank.clips.empty()) {
                doc.animClip = doc.animBank.clips.front().name;
                doc.animLoop = doc.animBank.clips.front().loop;
                doc.animDuration = computeClipDuration(doc.animBank.clips.front());
            }
        }
    }

    rebuildAtlas(assets);
    requestWorldCameraFrame = true;
    setStatus("Loaded " + virtualPath);
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    (void)assets;
    if (!doc.open || doc.virtualPath.empty()) {
        setStatus("Nothing to save");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    syncViewToAsset();

    const std::filesystem::path outPath =
        targetRoot / "sprites" / (doc.virtualPath + ".spr");
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        setStatus("Failed to create directories for save");
        return false;
    }

    const std::string text = slopengine::serializeSpriteAsset(doc.asset);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        setStatus("Failed to open for write: " + outPath.string());
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        setStatus("Failed writing: " + outPath.string());
        return false;
    }

    doc.dirty = false;
    if (doc.animDirty) {
        if (!saveAnim()) {
            return false;
        }
    }
    setStatus("Saved " + outPath.string());
    return true;
}

bool Editor::saveAnim() {
    if (!doc.open || doc.virtualPath.empty()) {
        return false;
    }
    if (!doc.hasAnim || doc.animBank.clips.empty()) {
        doc.animDirty = false;
        return true;
    }
    for (const slopengine::SpriteAnimClip& clip : doc.animBank.clips) {
        if (clip.frames.empty()) {
            setStatus("Anim clip \"" + clip.name + "\" has no frames");
            return false;
        }
    }

    const std::filesystem::path outPath =
        targetRoot / "sprites" / (doc.virtualPath + ".spanim");
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        setStatus("Failed to create directories for anim save");
        return false;
    }

    const std::string text = slopengine::serializeSpriteAnimBank(doc.animBank);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        setStatus("Failed to open anim for write: " + outPath.string());
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        setStatus("Failed writing anim: " + outPath.string());
        return false;
    }

    doc.animDirty = false;
    setStatus("Saved anim " + outPath.string());
    return true;
}

void Editor::rebuildAnimIndex() {
    doc.animBank.clipIndexByName.clear();
    for (std::size_t index = 0; index < doc.animBank.clips.size(); ++index) {
        doc.animBank.clipIndexByName.emplace(doc.animBank.clips[index].name, index);
    }
}

void Editor::duplicateSelectedFrame() {
    if (doc.selectedFrameIndex < 0 ||
        doc.selectedFrameIndex >= static_cast<int>(doc.asset.frames.size())) {
        return;
    }
    const slopengine::SpriteFrame& source =
        doc.asset.frames[static_cast<std::size_t>(doc.selectedFrameIndex)];
    slopengine::SpriteFrame copy = source;
    copy.id = source.id + "_copy";
    int suffix = 2;
    auto idTaken = [&](const std::string& id) {
        for (const slopengine::SpriteFrame& frame : doc.asset.frames) {
            if (frame.id == id) {
                return true;
            }
        }
        return false;
    };
    while (idTaken(copy.id)) {
        copy.id = source.id + "_" + std::to_string(suffix++);
    }
    doc.asset.frames.push_back(std::move(copy));
    selectFrameIndex(static_cast<int>(doc.asset.frames.size()) - 1);
    markDirty();
    doc.atlasDirty = true;
}

void Editor::ensureAnimBank() {
    if (doc.hasAnim && !doc.animBank.clips.empty()) {
        return;
    }
    doc.animBank = {};
    slopengine::SpriteAnimClip clip{};
    clip.name = "idle";
    clip.loop = true;
    if (!doc.asset.frames.empty()) {
        slopengine::SpriteAnimFrame frame{};
        frame.id = doc.asset.frames.front().id;
        frame.duration = 0.1f;
        clip.frames.push_back(std::move(frame));
    }
    doc.animBank.clips.push_back(std::move(clip));
    rebuildAnimIndex();
    doc.hasAnim = true;
    doc.animClip = "idle";
    doc.animDirty = true;
    doc.animDuration = clipDuration(doc.animClip);
}

slopengine::SpriteAnimClip* Editor::currentAnimClip() {
    if (!doc.hasAnim || doc.animClip.empty()) {
        return nullptr;
    }
    const auto it = doc.animBank.clipIndexByName.find(doc.animClip);
    if (it == doc.animBank.clipIndexByName.end() || it->second >= doc.animBank.clips.size()) {
        return nullptr;
    }
    return &doc.animBank.clips[it->second];
}

float Editor::clipDuration(const std::string& clip) const {
    const slopengine::SpriteAnimClip* found = findClip(doc.animBank, clip);
    if (found == nullptr) {
        return 0.0f;
    }
    return computeClipDuration(*found);
}

void Editor::playAnimClip(const std::string& clip, bool loop) {
    if (!doc.hasAnim) {
        return;
    }
    const slopengine::SpriteAnimClip* found = findClip(doc.animBank, clip);
    if (found == nullptr) {
        return;
    }
    doc.animClip = clip;
    doc.animLoop = loop;
    doc.animTime = 0.0f;
    doc.animPlaying = true;
    doc.animDuration = computeClipDuration(*found);
    doc.lastPreviewSoundFrameIndex = -1;
    applyAnimTime(doc, *found, 0.0f, loop);
}

void Editor::stopAnim() {
    doc.animPlaying = false;
    doc.lastPreviewSoundFrameIndex = -1;
}

void Editor::scrubAnim(float time) {
    if (!doc.hasAnim) {
        return;
    }
    const slopengine::SpriteAnimClip* found = findClip(doc.animBank, doc.animClip);
    if (found == nullptr) {
        return;
    }
    doc.animDuration = computeClipDuration(*found);
    doc.animTime = std::clamp(time, 0.0f, std::max(doc.animDuration, 0.0f));
    applyAnimTime(doc, *found, doc.animTime, doc.animLoop);
    doc.lastPreviewSoundFrameIndex = doc.animHoldIndex;
}

void Editor::tickAnim(float dt, slopengine::AssetStore& assets, slopengine::AudioWorld* audio) {
    if (!doc.animPlaying || !doc.hasAnim) {
        return;
    }
    const slopengine::SpriteAnimClip* found = findClip(doc.animBank, doc.animClip);
    if (found == nullptr) {
        doc.animPlaying = false;
        return;
    }
    doc.animDuration = computeClipDuration(*found);
    doc.animTime += dt * doc.animSpeed;
    if (!doc.animLoop && doc.animTime >= doc.animDuration) {
        doc.animTime = doc.animDuration;
        doc.animPlaying = false;
    } else if (doc.animLoop && doc.animDuration > 0.0f) {
        while (doc.animTime >= doc.animDuration) {
            doc.animTime -= doc.animDuration;
        }
    }
    const int previousIndex = doc.lastPreviewSoundFrameIndex;
    applyAnimTime(doc, *found, doc.animTime, doc.animLoop);
    firePreviewSound(doc, *found, previousIndex, doc.animHoldIndex, assets, audio);
    doc.lastPreviewSoundFrameIndex = doc.animHoldIndex;
}

void Editor::selectFrameIndex(int index) {
    if (index < 0 || index >= static_cast<int>(doc.asset.frames.size())) {
        return;
    }
    doc.selectedFrameIndex = index;
    doc.currentFrame = doc.asset.frames[static_cast<std::size_t>(index)].id;
    doc.animPlaying = false;
}

void loadViewCanvasSize(slopengine::AssetStore& assets, int& width, int& height) {
    width = 320;
    height = 200;
    if (!assets.hasData("view")) {
        return;
    }
    const auto resolved = assets.resolvePath(slopengine::AssetKind::Data, "view");
    if (!resolved) {
        return;
    }
    std::ifstream in(*resolved);
    if (!in) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    parseViewCanvasFromSource(text, width, height);
}

}
