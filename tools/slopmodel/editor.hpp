#pragma once

#include "assets/anim_loader.hpp"
#include "assets/asset_store.hpp"
#include "assets/rigged_assets.hpp"

#include <raylib.h>

#include <filesystem>
#include <string>
#include <vector>

namespace slopmodel {

enum class PreviewMode {
    World,
    FirstPerson,
};

/** A geo shown hanging on one attachment while editing; never saved. */
struct AttachmentPreview {
    std::string geoPath;
    Model model{};
};

struct Document {
    std::string virtualPath;
    std::string packageId;
    slopengine::GeoAsset geo{};
    std::string skeletonPath;
    slopengine::AnimAsset anim{};
    bool hasAnim = false;
    Model model{};
    bool open = false;
    bool geoDirty = false;
    bool animDirty = false;

    std::string clip;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool loop = true;
    float lastFrame = -1.0f;
    std::string lastEvent;
    float lastEventTimer = 0.0f;

    int selectedAttachment = -1;
    int selectedBone = -1;
    std::vector<AttachmentPreview> previews;
    float worldScale = 1.0f;
};

struct ClipInfo {
    float fps = 0.0f;
    int frameCount = 0;
    float duration = 0.0f;
    bool valid = false;
};

struct Editor {
    std::filesystem::path targetRoot;
    std::string targetPackageId;
    PreviewMode mode = PreviewMode::World;
    Document doc;
    std::string statusMessage;
    float statusTimer = 0.0f;
    bool requestWorldCameraFrame = false;
    bool showSkeleton = false;
    bool showMesh = true;
    bool showAttachments = true;
    bool shaded = true;
    bool twoSided = false;

    void setStatus(std::string message, float seconds = 3.0f);
    bool load(slopengine::AssetStore& assets, const std::string& virtualPath);
    void close();
    bool save(slopengine::AssetStore& assets);
    void markGeoDirty();
    void markAnimDirty();

    bool skinned() const;
    const slopengine::AnimBank* bank(slopengine::AssetStore& assets) const;
    ClipInfo clipInfo(slopengine::AssetStore& assets) const;
    slopengine::AnimClip* currentClipMeta();
    void playClip(const std::string& name, bool loop);
    void stop();
    void scrub(slopengine::AssetStore& assets, float time);
    void tick(float dt, slopengine::AssetStore& assets);
    void applyPose(slopengine::AssetStore& assets);

    int addAttachment();
    void removeAttachment(int index);
    bool setAttachmentPreview(slopengine::AssetStore& assets, int index, const std::string& geoPath);
    void clearAttachmentPreview(int index);
    /** Model-space pose of attachment @p index at the current frame. */
    Transform attachmentPose(int index) const;

    int addEvent(int frame, const std::string& name);
    void removeEvent(int index);
};

/** Builds a raylib matrix from a transform the way the engine's transform system does. */
Matrix transformMatrix(const Transform& transform);

/** The socket-space matrix of a geo placed by @p view. */
Matrix viewPlacementMatrix(const slopengine::GeoView& view);

}
