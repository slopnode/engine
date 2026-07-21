#pragma once

#include "assets/asset_store.hpp"
#include "assets/sprite_anim_loader.hpp"
#include "assets/sprite_loader.hpp"
#include "render/components.hpp"

#include <filesystem>
#include <string>
#include <vector>

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

    int selectedFrameIndex = 0;
    int selectedRot = 0;

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

    void setStatus(std::string message, float seconds = 3.0f);
    void applyViewFromAsset();
    void syncViewToAsset();
    void markDirty();
    void rebuildAtlas(slopengine::AssetStore& assets);
    bool loadSprite(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    bool saveAnim();
    void rebuildAnimIndex();
    void duplicateSelectedFrame();
    void ensureAnimBank();
    slopengine::SpriteAnimClip* currentAnimClip();
    void tickAnim(float dt);
    void scrubAnim(float time);
    void playAnimClip(const std::string& clip, bool loop);
    void stopAnim();
    void selectFrameIndex(int index);
    float clipDuration(const std::string& clip) const;
};

void loadViewCanvasSize(slopengine::AssetStore& assets, int& width, int& height);

}
