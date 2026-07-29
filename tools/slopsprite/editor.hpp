#pragma once

#include "assets/asset_store.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "assets/sprite_loader.hpp"
#include "render/components.hpp"

#include <raylib.h>

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {
class AudioWorld;
}

namespace slopsprite {

enum class PreviewMode {
    World,
    FirstPerson,
    Align,
};

enum class FrameRotationMode {
    None,
    Five,
    Eight,
    Custom,
};

FrameRotationMode detectFrameRotationMode(const slopengine::SpriteFrame& frame);
void applyFrameRotationMode(slopengine::SpriteFrame& frame, FrameRotationMode mode);
void syncFiveAngleMirrors(slopengine::SpriteFrame& frame);
int frameRotationAuthorCount(FrameRotationMode mode);
int frameRotationAuthorIndex(FrameRotationMode mode, int slot);
int clampSelectedRotToMode(FrameRotationMode mode, int selectedRot);

struct EditorDocument {
    std::string virtualPath;
    slopengine::SpriteAsset asset{};
    slopengine::SpriteAtlas atlas{};
    slopengine::SpriteAnimBank animBank{};
    bool hasAnim = false;
    bool dirty = false;
    bool animDirty = false;
    bool atlasDirty = true;
    bool open = false;

    std::string currentFrame = "A";
    float facingYaw = 0.0f;
    float worldScale = 1.0f;

    slopengine::ViewSprite viewSprite{};
    float eyeOffsetX = 0.0f;
    float eyeOffsetY = 0.0f;
    float eyeOffsetZ = 0.0f;
    bool hasMuzzle = false;
    float muzzleX = 0.0f;
    float muzzleY = 0.0f;
    bool muzzleSelected = false;

    std::string animClip;
    float animTime = 0.0f;
    float animSpeed = 1.0f;
    bool animPlaying = false;
    bool animLoop = true;
    float animDuration = 0.0f;
    bool animTweenRotation = false;
    bool animTweenScale = false;
    bool animTweenTranslate = false;
    float animTransformBlend = 0.0f;
    std::string animNextFrame;
    int animHoldIndex = -1;
    int lastPreviewSoundFrameIndex = -1;

    int selectedFrameIndex = 0;
    int selectedRot = 0;
    int selectedOverlayHoldIndex = -1;
    int selectedOverlayIndex = -1;

    bool onionEnabled = false;
    int onionFrameIndex = 0;
    int onionRot = 0;
    float alignZoom = 2.0f;
};

struct Editor {
    std::filesystem::path targetRoot;
    std::string targetPackageId;
    PreviewMode mode = PreviewMode::World;
    EditorDocument doc;
    int viewCanvasW = 320;
    int viewCanvasH = 200;
    std::string filter;
    std::string statusMessage;
    float statusTimer = 0.0f;
    bool requestWorldCameraFrame = false;
    bool showSpriteMasks = false;
    bool showNewSpriteModal = false;
    char newSpritePathBuf[256] = {};

    void setStatus(std::string message, float seconds = 3.0f);
    void applyViewFromAsset();
    void syncViewToAsset();
    void markDirty();
    void rebuildAtlas(slopengine::AssetStore& assets);
    bool loadSprite(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool newSprite(const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    bool saveAnim();
    void rebuildAnimIndex();
    void duplicateSelectedFrame();
    void ensureAnimBank();
    slopengine::SpriteAnimClip* currentAnimClip();
    void tickAnim(float dt, slopengine::AssetStore& assets, slopengine::AudioWorld* audio);
    void scrubAnim(float time);
    void playAnimClip(const std::string& clip, bool loop);
    void stopAnim();
    void selectFrameIndex(int index);
    float clipDuration(const std::string& clip) const;
};

void loadViewCanvasSize(slopengine::AssetStore& assets, int& width, int& height);

bool previewShowingTween(const EditorDocument& doc);
Color previewClearColor(const EditorDocument& doc, PreviewMode mode);
const char* previewPoseLabel(const EditorDocument& doc, PreviewMode mode);
Color previewPoseLabelColor(const EditorDocument& doc, PreviewMode mode);

/** Active (overlay ...) layer for FP / World preview at the current anim scrub time. */
struct PreviewOverlayDraw {
    int layer = 1;
    float x = 0.0f;
    float y = 0.0f;
    int holdIndex = -1;
    int overlayIndex = -1;
    std::string spritePath;
    std::string frameId;
    bool tweenRotation = false;
    bool tweenScale = false;
    bool tweenTranslate = false;
    float transformBlend = 0.0f;
    std::string nextFrame;
};

/** Fills @p out with overlays visible at doc.animTime for the current clip. */
void collectPreviewOverlays(
    const EditorDocument& doc,
    slopengine::AssetStore& assets,
    std::vector<PreviewOverlayDraw>& out);

}
