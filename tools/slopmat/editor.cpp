#include "editor.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace slopmat {

namespace {

bool writeTextFile(const std::filesystem::path& outPath, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        error = "Failed to create directories for " + outPath.string();
        return false;
    }
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Failed to open for write: " + outPath.string();
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        error = "Failed writing: " + outPath.string();
        return false;
    }
    return true;
}

Texture2D repeatWrapped(Texture2D texture) {
    if (texture.id != 0) {
        SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    }
    return texture;
}

bool graphHasOutput(const slopengine::TextureGraph& graph, const char* name) {
    return graph.findOutput(name) != nullptr;
}

} // namespace

const char* docKindLabel(DocKind kind) {
    switch (kind) {
    case DocKind::Material: return "material";
    case DocKind::TextureGraph: return "texture graph";
    case DocKind::TextureAnim: return "texture animation";
    case DocKind::Texture: return "texture";
    }
    return "";
}

void Editor::setStatus(std::string message, float seconds) {
    statusMessage = std::move(message);
    statusTimer = seconds;
}

void Editor::markDirty() {
    doc.dirty = true;
    doc.previewDirty = true;
}

void Editor::close() {
    releasePreview();
    doc = {};
    undoStack.clear();
    redoStack.clear();
    undoAnchorValid = false;
}

void Editor::releasePreview() {
    if (doc.ownsPreviews) {
        for (PreviewSlot& slot : doc.previews) {
            if (slot.texture.id != 0) {
                UnloadTexture(slot.texture);
            }
        }
    }
    doc.previews.clear();
    doc.ownsPreviews = false;
}

bool Editor::loadMaterial(slopengine::AssetStore& assets, const std::string& virtualPath) {
    if (!assets.hasMaterial(virtualPath)) {
        setStatus("Material not found: " + virtualPath);
        return false;
    }
    slopengine::MaterialAsset asset{};
    if (!slopengine::parseMaterialAsset(assets.getMaterialSource(virtualPath), asset)) {
        setStatus("Failed to parse material: " + virtualPath);
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::Material;
    doc.material = std::move(asset);
    doc.open = true;
    doc.previewDirty = true;
    return true;
}

bool Editor::loadTextureGraph(slopengine::AssetStore& assets, const std::string& virtualPath) {
    if (!assets.hasTextureGraph(virtualPath)) {
        setStatus("Texture graph not found: " + virtualPath);
        return false;
    }
    slopengine::TextureGraph graph{};
    std::string error;
    if (!slopengine::parseTextureGraph(assets.getTextureGraphSource(virtualPath), graph, &error)) {
        setStatus("Failed to parse texture graph: " + error);
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::TextureGraph;
    doc.graph = std::move(graph);
    doc.open = true;
    doc.previewDirty = true;
    ensureComposite();
    return true;
}

void Editor::ensureComposite() {
    slopengine::TextureGraph& graph = doc.graph;
    const slopengine::TextureGraphNode* composite = nullptr;
    if (const slopengine::TextureGraphOutput* albedo = graph.findOutput("albedo")) {
        const slopengine::TextureGraphNode* node = graph.findNode(albedo->source);
        if (node != nullptr && node->kind == slopengine::TextureGraphNodeKind::Stack) {
            composite = node;
        }
    }
    if (composite == nullptr) {
        for (const slopengine::TextureGraphNode& node : graph.nodes) {
            if (node.kind == slopengine::TextureGraphNodeKind::Stack) {
                composite = &node;
                break;
            }
        }
    }
    if (composite == nullptr) {
        slopengine::TextureGraphNode stack{};
        stack.id = graph.findNode("composite") == nullptr ? "composite" : "composite-" + std::to_string(graph.nodes.size());
        stack.kind = slopengine::TextureGraphNodeKind::Stack;
        stack.width = 0;
        stack.height = 0;
        graph.nodes.push_back(stack);
        composite = &graph.nodes.back();
        doc.dirty = true;
    }
    doc.compositeId = composite->id;
    if (graph.findOutput("albedo") == nullptr) {
        graph.outputs.push_back({"albedo", doc.compositeId});
        doc.dirty = true;
    }
}

bool Editor::loadTextureAnim(slopengine::AssetStore& assets, const std::string& virtualPath) {
    if (!assets.hasTextureAnim(virtualPath)) {
        setStatus("Texture animation not found: " + virtualPath);
        return false;
    }
    slopengine::TextureAnimBank bank{};
    if (!slopengine::parseTextureAnimBank(assets.getTextureAnimSource(virtualPath), bank)) {
        setStatus("Failed to parse texture animation: " + virtualPath);
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::TextureAnim;
    doc.anim = std::move(bank);
    doc.open = true;
    doc.previewDirty = true;
    return true;
}

bool Editor::newTextureAnim(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a virtual path first");
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::TextureAnim;
    doc.anim = {};
    slopengine::TextureAnimClip clip{};
    clip.name = "default";
    clip.loop = true;
    doc.anim.clips.push_back(clip);
    doc.anim.clipIndexByName.emplace("default", 0);
    doc.open = true;
    doc.dirty = true;
    doc.previewDirty = true;
    setStatus("New texture animation " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::loadTexture(slopengine::AssetStore& assets, const std::string& virtualPath) {
    if (!assets.hasTexture(virtualPath)) {
        setStatus("Texture not found: " + virtualPath);
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::Texture;
    doc.open = true;
    doc.previewDirty = true;
    return true;
}

bool Editor::newMaterial(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a virtual path first");
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::Material;
    doc.material = {};
    doc.open = true;
    doc.dirty = true;
    doc.previewDirty = true;
    setStatus("New material " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::newTextureGraph(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        setStatus("Enter a virtual path first");
        return false;
    }
    close();
    doc.virtualPath = virtualPath;
    doc.kind = DocKind::TextureGraph;
    doc.graph = {};

    slopengine::TextureGraphNode base{};
    base.id = "base";
    base.kind = slopengine::TextureGraphNodeKind::Checker;
    base.color = {0.6f, 0.6f, 0.6f, 1.0f};
    base.colorB = {0.4f, 0.4f, 0.4f, 1.0f};
    doc.graph.nodes.push_back(base);

    slopengine::TextureGraphNode stack{};
    stack.id = "composite";
    stack.kind = slopengine::TextureGraphNodeKind::Stack;
    stack.width = 0;
    stack.height = 0;
    slopengine::TextureGraphLayer baseLayer{};
    baseLayer.source = "base";
    baseLayer.repeat = true;
    stack.layers.push_back(baseLayer);
    doc.graph.nodes.push_back(stack);

    doc.graph.outputs.push_back({"albedo", "composite"});
    doc.compositeId = "composite";
    doc.open = true;
    doc.dirty = true;
    doc.previewDirty = true;
    setStatus("New texture graph " + virtualPath + " (unsaved)");
    return true;
}

bool Editor::save(slopengine::AssetStore& assets) {
    if (!doc.open || doc.virtualPath.empty()) {
        setStatus("Nothing to save");
        return false;
    }
    if (doc.kind == DocKind::Texture) {
        setStatus("Textures are view-only here");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }

    std::string error;
    if (doc.kind == DocKind::Material) {
        const std::filesystem::path outPath = targetRoot / "materials" / (doc.virtualPath + ".mat");
        if (!writeTextFile(outPath, slopengine::serializeMaterialAsset(doc.material), error)) {
            setStatus(error);
            return false;
        }
        assets.invalidateMaterial(doc.virtualPath);
    } else if (doc.kind == DocKind::TextureAnim) {
        for (const slopengine::TextureAnimClip& clip : doc.anim.clips) {
            if (clip.name.empty() || clip.frames.empty()) {
                doc.problem = "every clip needs a name and at least one frame";
                setStatus("Not saved: " + doc.problem);
                return false;
            }
            for (const slopengine::TextureAnimFrame& frame : clip.frames) {
                if (frame.texture.empty() || frame.duration <= 0.0f) {
                    doc.problem = "clip '" + clip.name + "' has a frame without a texture or duration";
                    setStatus("Not saved: " + doc.problem);
                    return false;
                }
            }
        }
        if (doc.anim.clips.empty()) {
            doc.problem = "a texture animation needs at least one clip";
            setStatus("Not saved: " + doc.problem);
            return false;
        }
        const std::filesystem::path outPath = targetRoot / "textures" / (doc.virtualPath + ".texanim");
        if (!writeTextFile(outPath, slopengine::serializeTextureAnimBank(doc.anim), error)) {
            setStatus(error);
            return false;
        }
        assets.invalidateTextureAnim(doc.virtualPath);
    } else {
        if (!slopengine::validateTextureGraph(doc.graph, error)) {
            doc.problem = error;
            setStatus("Not saved: " + error);
            return false;
        }
        const std::filesystem::path outPath = targetRoot / "textures" / (doc.virtualPath + ".texgraph");
        if (!writeTextFile(outPath, slopengine::serializeTextureGraph(doc.graph), error)) {
            setStatus(error);
            return false;
        }
        assets.invalidateTextureGraph(doc.virtualPath);
    }

    doc.dirty = false;
    setStatus("Saved " + doc.virtualPath);
    return true;
}

void Editor::rebuildPreview(slopengine::AssetStore& assets) {
    releasePreview();
    doc.previewDirty = false;
    doc.problem.clear();
    if (!doc.open) {
        return;
    }

    switch (doc.kind) {
    case DocKind::Texture:
        doc.previews.push_back({"texture", repeatWrapped(assets.getTexture(doc.virtualPath))});
        break;
    case DocKind::TextureAnim: {
        if (doc.anim.clips.empty()) {
            doc.problem = "no clips";
            break;
        }
        doc.animClip = std::clamp(doc.animClip, 0, static_cast<int>(doc.anim.clips.size()) - 1);
        const slopengine::TextureAnimClip& clip = doc.anim.clips[static_cast<std::size_t>(doc.animClip)];
        for (std::size_t i = 0; i < clip.frames.size(); ++i) {
            const Texture2D frame = clip.frames[i].texture.empty() ? Texture2D{} : assets.getTexture(clip.frames[i].texture);
            if (frame.id == 0 && doc.problem.empty()) {
                doc.problem = "frame " + std::to_string(i) + " has no texture";
            }
            doc.previews.push_back({"frame " + std::to_string(i), repeatWrapped(frame)});
        }
        break;
    }
    case DocKind::Material: {
        const slopengine::MaterialAsset& m = doc.material;
        Texture2D albedo{};
        if (!m.albedoTexture.empty()) {
            albedo = assets.getTexture(m.albedoTexture);
        } else if (!m.textureAnimPath.empty()) {
            albedo = assets.resolveTextureAnimFrame(m.textureAnimPath, "default", 0);
        } else if (!m.textureGraphPath.empty()) {
            albedo = assets.resolveTextureGraphOutput(m.textureGraphPath, "albedo");
            if (albedo.id == 0) {
                doc.problem = "texture graph '" + m.textureGraphPath + "' has no usable albedo output (see log)";
            }
        }
        doc.previews.push_back({"albedo", repeatWrapped(albedo)});

        Texture2D emission{};
        if (!m.emissionTexture.empty()) {
            emission = assets.getTexture(m.emissionTexture);
        } else if (!m.textureGraphPath.empty()) {
            const slopengine::TextureGraph* graph = assets.getTextureGraph(m.textureGraphPath);
            if (graph != nullptr && graphHasOutput(*graph, "emission")) {
                emission = assets.resolveTextureGraphOutput(m.textureGraphPath, "emission");
            }
        }
        if (emission.id != 0) {
            doc.previews.push_back({"emission", repeatWrapped(emission)});
        }
        break;
    }
    case DocKind::TextureGraph: {
        doc.ownsPreviews = true;
        std::string error;
        if (!slopengine::validateTextureGraph(doc.graph, error)) {
            doc.problem = error;
            return;
        }
        const slopengine::TextureGraphImageResolver resolveImage =
            [&assets](std::string_view texturePath, Image& out) {
                const auto resolved = assets.resolvePath(slopengine::AssetKind::Texture, texturePath);
                if (!resolved) {
                    return false;
                }
                out = LoadImage(resolved->string().c_str());
                return out.data != nullptr;
            };
        for (const slopengine::TextureGraphOutput& output : doc.graph.outputs) {
            Image image{};
            if (!slopengine::evaluateTextureGraph(doc.graph, output.name, resolveImage, image, error)) {
                if (doc.problem.empty()) {
                    doc.problem = error;
                }
                doc.previews.push_back({output.name, Texture2D{}});
                continue;
            }
            Texture2D texture = LoadTextureFromImage(image);
            UnloadImage(image);
            doc.previews.push_back({output.name, repeatWrapped(texture)});
        }
        break;
    }
    }
}

bool Editor::exportSlot(slopengine::AssetStore& assets, const std::string& slotName, std::string& writtenPath) {
    if (!doc.open || doc.kind == DocKind::Texture) {
        setStatus("Nothing to export");
        return false;
    }
    if (targetRoot.empty()) {
        setStatus("No --target package");
        return false;
    }
    Image image{};
    std::string error;
    if (doc.kind == DocKind::TextureGraph) {
        const slopengine::TextureGraphImageResolver resolveImage = [&assets](std::string_view texturePath, Image& out) {
            const auto resolved = assets.resolvePath(slopengine::AssetKind::Texture, texturePath);
            if (!resolved) {
                return false;
            }
            out = LoadImage(resolved->string().c_str());
            return out.data != nullptr;
        };
        if (!slopengine::evaluateTextureGraph(doc.graph, slotName, resolveImage, image, error)) {
            setStatus("Export failed: " + error);
            return false;
        }
    } else {
        const PreviewSlot* slot = nullptr;
        for (const PreviewSlot& candidate : doc.previews) {
            if (candidate.name == slotName) {
                slot = &candidate;
            }
        }
        if (slot == nullptr || slot->texture.id == 0) {
            setStatus("Export failed: no image for " + slotName);
            return false;
        }
        image = LoadImageFromTexture(slot->texture);
        if (image.data == nullptr) {
            setStatus("Export failed: could not read back " + slotName);
            return false;
        }
    }
    std::string stem = doc.virtualPath + "-" + slotName;
    std::replace(stem.begin(), stem.end(), ' ', '-');
    const std::filesystem::path outPath = targetRoot / "textures" / (stem + ".png");
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    const bool ok = !ec && ExportImage(image, outPath.string().c_str());
    UnloadImage(image);
    if (!ok) {
        setStatus("Export failed: " + outPath.string());
        return false;
    }
    writtenPath = stem;
    setStatus("Exported " + outPath.string());
    return true;
}

std::string Editor::serializeDoc() const {
    switch (doc.kind) {
    case DocKind::Material: return slopengine::serializeMaterialAsset(doc.material);
    case DocKind::TextureGraph: return slopengine::serializeTextureGraph(doc.graph);
    case DocKind::TextureAnim: return slopengine::serializeTextureAnimBank(doc.anim);
    case DocKind::Texture: return {};
    }
    return {};
}

bool Editor::restoreDoc(const std::string& text) {
    switch (doc.kind) {
    case DocKind::Material: {
        slopengine::MaterialAsset asset{};
        if (!slopengine::parseMaterialAsset(text, asset)) {
            return false;
        }
        doc.material = std::move(asset);
        break;
    }
    case DocKind::TextureGraph: {
        slopengine::TextureGraph graph{};
        std::string error;
        if (!slopengine::parseTextureGraph(text, graph, &error)) {
            return false;
        }
        doc.graph = std::move(graph);
        ensureComposite();
        break;
    }
    case DocKind::TextureAnim: {
        slopengine::TextureAnimBank bank{};
        if (!slopengine::parseTextureAnimBank(text, bank)) {
            return false;
        }
        doc.anim = std::move(bank);
        break;
    }
    case DocKind::Texture:
        return false;
    }
    markDirty();
    return true;
}

void Editor::captureUndo(bool itemActive) {
    if (!doc.open || doc.kind == DocKind::Texture) {
        undoAnchorValid = false;
        return;
    }
    const std::string now = serializeDoc();
    if (!undoAnchorValid) {
        undoAnchor = now;
        undoAnchorValid = true;
        return;
    }
    if (itemActive || now == undoAnchor) {
        return;
    }
    undoStack.push_back(undoAnchor);
    if (undoStack.size() > 100) {
        undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
    undoAnchor = now;
}

void Editor::undo() {
    if (undoStack.empty()) {
        setStatus("Nothing to undo");
        return;
    }
    const std::string current = serializeDoc();
    const std::string previous = undoStack.back();
    undoStack.pop_back();
    if (restoreDoc(previous)) {
        redoStack.push_back(current);
        undoAnchor = previous;
    } else {
        setStatus("Undo failed: snapshot would not parse");
    }
}

void Editor::redo() {
    if (redoStack.empty()) {
        setStatus("Nothing to redo");
        return;
    }
    const std::string current = serializeDoc();
    const std::string next = redoStack.back();
    redoStack.pop_back();
    if (restoreDoc(next)) {
        undoStack.push_back(current);
        undoAnchor = next;
    } else {
        setStatus("Redo failed: snapshot would not parse");
    }
}

}
