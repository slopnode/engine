#pragma once

#include <raylib.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

enum class TextureGraphNodeKind {
    Image,
    Color,
    Checker,
    Gradient,
    Noise,
    Bricks,
    Stripes,
    GradientMap,
    Transform,
    Levels,
    Hsv,
    Invert,
    Blur,
    Sharpen,
    Edges,
    Stack,
};

enum class TextureGraphBlend {
    Normal,
    Multiply,
    Add,
    Screen,
};

enum class TextureGraphNoiseType {
    Value,
    Perlin,
    Worley,
};

/** One entry in a stack node, composited bottom to top. */
struct TextureGraphLayer {
    std::string source; /**< Node id. */
    std::string mask; /**< Optional node id whose luminance scales this layer's alpha, sampled in canvas space. */
    TextureGraphBlend blend = TextureGraphBlend::Normal;
    float opacity = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    bool repeat = false; /**< Tile the source across the canvas instead of placing it once. */
    Vector4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

struct TextureGraphStop {
    float position = 0.0f;
    Vector4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

/** One node; the fields a node reads depend on its kind. */
struct TextureGraphNode {
    std::string id;
    TextureGraphNodeKind kind = TextureGraphNodeKind::Image;
    std::string texture; /**< Image: virtual texture path. */
    std::string input; /**< Unary nodes (GradientMap, Transform, Levels, Hsv, Invert, Blur, Sharpen, Edges): source node id. */
    Vector4 color{1.0f, 1.0f, 1.0f, 1.0f}; /**< Color / Checker first cell / Gradient start / Bricks brick / Stripes first. */
    Vector4 colorB{0.0f, 0.0f, 0.0f, 1.0f}; /**< Checker second cell / Gradient end / Bricks mortar / Stripes second. */
    int width = 64; /**< Generators; Stack canvas and Transform output, where 0 means "size of the source". */
    int height = 64;
    int cellsX = 8; /**< Checker cell count. */
    int cellsY = 8;
    float angle = 0.0f; /**< Gradient and Stripes direction, Transform rotation; degrees. */
    float scale = 1.0f; /**< Noise lattice cells across the image. */
    int seed = 0;
    TextureGraphNoiseType noiseType = TextureGraphNoiseType::Perlin;
    int octaves = 1;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    int brickWidth = 32; /**< Bricks, pixels. */
    int brickHeight = 16;
    int mortar = 2;
    float rowOffset = 0.5f; /**< Bricks: fraction of a brick each row shifts. */
    int stripe = 8; /**< Stripes: band width in pixels. */
    float scaleX = 1.0f; /**< Transform magnification. */
    float scaleY = 1.0f;
    bool flipH = false;
    bool flipV = false;
    float inBlack = 0.0f; /**< Levels. */
    float inWhite = 1.0f;
    float gamma = 1.0f;
    float outBlack = 0.0f;
    float outWhite = 1.0f;
    float hue = 0.0f; /**< Hsv: hue shift in degrees. */
    float saturation = 1.0f;
    float value = 1.0f;
    float radius = 1.0f; /**< Blur radius in pixels. */
    float amount = 1.0f; /**< Sharpen strength. */
    std::vector<TextureGraphStop> stops; /**< GradientMap. */
    std::vector<TextureGraphLayer> layers; /**< Stack. */
};

/** Maps a material slot name ("albedo", "emission") to the node that produces it. */
struct TextureGraphOutput {
    std::string name;
    std::string source;
};

struct TextureGraph {
    std::vector<TextureGraphNode> nodes;
    std::vector<TextureGraphOutput> outputs;

    const TextureGraphNode* findNode(std::string_view id) const;
    TextureGraphNode* findNode(std::string_view id);
    const TextureGraphOutput* findOutput(std::string_view name) const;
};

const char* textureGraphNodeKindName(TextureGraphNodeKind kind);
bool textureGraphNodeKindFromName(std::string_view name, TextureGraphNodeKind& out);
const char* textureGraphBlendName(TextureGraphBlend blend);
bool textureGraphBlendFromName(std::string_view name, TextureGraphBlend& out);
const char* textureGraphNoiseTypeName(TextureGraphNoiseType type);
bool textureGraphNoiseTypeFromName(std::string_view name, TextureGraphNoiseType& out);

/** True for kinds that read one source node through @ref TextureGraphNode::input. */
bool textureGraphKindHasInput(TextureGraphNodeKind kind);

/** Every node id @p node refers to: layers, masks and input. */
std::vector<std::string> textureGraphNodeReferences(const TextureGraphNode& node);

/** Parses .texgraph text into @p graph; a validation or syntax message lands in @p error when given. */
bool parseTextureGraph(std::string_view source, TextureGraph& graph, std::string* error = nullptr);

/** Serializes @p graph to .texgraph text. */
std::string serializeTextureGraph(const TextureGraph& graph);

/** Checks ids, references, outputs and cycles; fills @p error on the first problem. */
bool validateTextureGraph(const TextureGraph& graph, std::string& error);

/** Loads the PNG behind a virtual texture path into @p out; false when missing. Caller owns @p out. */
using TextureGraphImageResolver = std::function<bool(std::string_view texturePath, Image& out)>;

/** Evaluates @p outputName on the CPU into an RGBA8 image the caller must UnloadImage. */
bool evaluateTextureGraph(
    const TextureGraph& graph,
    std::string_view outputName,
    const TextureGraphImageResolver& resolveImage,
    Image& out,
    std::string& error);

}
