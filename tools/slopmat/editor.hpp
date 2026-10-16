#pragma once

#include "assets/asset_store.hpp"
#include "assets/material_loader.hpp"
#include "assets/texture_anim_loader.hpp"
#include "assets/texture_graph.hpp"

#include <raylib.h>

#include <filesystem>
#include <string>
#include <vector>

namespace slopmat {

enum class DocKind {
    Material,     // materials/*.mat
    TextureGraph, // textures/*.texgraph
    TextureAnim,  // textures/*.texanim
    Texture,      // textures/*.png, view-only
};

/** One uploaded preview image: a material slot, a graph output, or the raw texture. */
struct PreviewSlot {
    std::string name;
    Texture2D texture{};
};

struct EditorDocument {
    std::string virtualPath;
    DocKind kind = DocKind::Material;
    bool open = false;
    bool dirty = false;
    slopengine::MaterialAsset material{};
    slopengine::TextureGraph graph{};
    std::string compositeId;
    slopengine::TextureAnimBank anim{};
    int animClip = 0;
    std::string problem;
    std::vector<PreviewSlot> previews;
    bool ownsPreviews = false;
    bool previewDirty = true;
};

struct Editor {
    std::filesystem::path targetRoot;
    EditorDocument doc;
    std::string statusMessage;
    float statusTimer = 0.0f;
    bool showNewModal = false;
    DocKind newModalKind = DocKind::Material;
    char newPathBuf[256] = {};
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string undoAnchor;
    bool undoAnchorValid = false;

    void setStatus(std::string message, float seconds = 3.0f);
    void markDirty();
    void close();
    bool loadMaterial(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadTextureGraph(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadTextureAnim(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool loadTexture(slopengine::AssetStore& assets, const std::string& virtualPath);
    bool newMaterial(const std::string& virtualPath);
    bool newTextureGraph(const std::string& virtualPath);
    bool newTextureAnim(const std::string& virtualPath);
    bool save(slopengine::AssetStore& assets);
    bool exportSlot(slopengine::AssetStore& assets, const std::string& slotName, std::string& writtenPath);
    std::string serializeDoc() const;
    bool restoreDoc(const std::string& text);
    void captureUndo(bool itemActive);
    void undo();
    void redo();
    void rebuildPreview(slopengine::AssetStore& assets);
    void ensureComposite();
    void releasePreview();
};

const char* docKindLabel(DocKind kind);

}
