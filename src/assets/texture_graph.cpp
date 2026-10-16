#include "assets/texture_graph.hpp"

#include "core/sexpr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace slopengine {

namespace {

constexpr float kPi = 3.14159265358979f;

struct KindName {
    TextureGraphNodeKind kind;
    const char* name;
};

constexpr KindName kKindNames[] = {
    {TextureGraphNodeKind::Image, "image"},
    {TextureGraphNodeKind::Color, "color"},
    {TextureGraphNodeKind::Checker, "checker"},
    {TextureGraphNodeKind::Gradient, "gradient"},
    {TextureGraphNodeKind::Noise, "noise"},
    {TextureGraphNodeKind::Bricks, "bricks"},
    {TextureGraphNodeKind::Stripes, "stripes"},
    {TextureGraphNodeKind::GradientMap, "gradient-map"},
    {TextureGraphNodeKind::Transform, "transform"},
    {TextureGraphNodeKind::Levels, "levels"},
    {TextureGraphNodeKind::Hsv, "hsv"},
    {TextureGraphNodeKind::Invert, "invert"},
    {TextureGraphNodeKind::Blur, "blur"},
    {TextureGraphNodeKind::Sharpen, "sharpen"},
    {TextureGraphNodeKind::Edges, "edges"},
    {TextureGraphNodeKind::Stack, "stack"},
};

struct BlendName {
    TextureGraphBlend blend;
    const char* name;
};

constexpr BlendName kBlendNames[] = {
    {TextureGraphBlend::Normal, "normal"},
    {TextureGraphBlend::Multiply, "multiply"},
    {TextureGraphBlend::Add, "add"},
    {TextureGraphBlend::Screen, "screen"},
};

struct NoiseName {
    TextureGraphNoiseType type;
    const char* name;
};

constexpr NoiseName kNoiseNames[] = {
    {TextureGraphNoiseType::Value, "value"},
    {TextureGraphNoiseType::Perlin, "perlin"},
    {TextureGraphNoiseType::Worley, "worley"},
};

void setError(std::string* error, const std::string& message, int line, int column) {
    if (error != nullptr) {
        *error = message + " (line " + std::to_string(line) + ", column " + std::to_string(column) + ")";
    }
}

bool readNumbers(const Sexpr& form, std::size_t count, float* out) {
    if (!form.isList() || form.list.size() != count + 1) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!form.list[i + 1].isNumber()) {
            return false;
        }
        out[i] = static_cast<float>(form.list[i + 1].number);
    }
    return true;
}

bool readVec4(const Sexpr& form, Vector4& out) {
    float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!readNumbers(form, 4, v)) {
        return false;
    }
    out = {v[0], v[1], v[2], v[3]};
    return true;
}

bool readInt2(const Sexpr& form, int& a, int& b) {
    float v[2] = {};
    if (!readNumbers(form, 2, v)) {
        return false;
    }
    a = static_cast<int>(v[0]);
    b = static_cast<int>(v[1]);
    return true;
}

bool readInt(const Sexpr& form, int& out) {
    float v = 0.0f;
    if (!readNumbers(form, 1, &v)) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

bool readFlag(const Sexpr& form, bool& out) {
    if (!form.isList()) {
        return false;
    }
    if (form.list.size() == 1) {
        out = true;
        return true;
    }
    float flag = 1.0f;
    if (!readNumbers(form, 1, &flag)) {
        return false;
    }
    out = flag != 0.0f;
    return true;
}

bool readRef(const Sexpr& form, std::string& out) {
    if (!form.isList() || form.list.size() != 2 || !form.list[1].isString()) {
        return false;
    }
    out = form.list[1].text;
    return true;
}

bool parseLayerForm(const Sexpr& form, TextureGraphLayer& layer, std::string* error) {
    if (form.list.size() < 2 || !form.list[1].isString() || form.list[1].text.empty()) {
        setError(error, "(layer \"node-id\" ...)", form.line, form.column);
        return false;
    }
    layer = {};
    layer.source = form.list[1].text;
    for (std::size_t i = 2; i < form.list.size(); ++i) {
        const Sexpr& field = form.list[i];
        if (!field.isList() || field.list.empty() || field.list[0].kind != SexprKind::Atom) {
            setError(error, "layer field expected", field.line, field.column);
            return false;
        }
        const std::string& tag = field.list[0].text;
        if (tag == "blend") {
            if (field.list.size() != 2 || field.list[1].kind != SexprKind::Atom ||
                !textureGraphBlendFromName(field.list[1].text, layer.blend)) {
                setError(error, "(blend normal|multiply|add|screen)", field.line, field.column);
                return false;
            }
        } else if (tag == "opacity") {
            if (!readNumbers(field, 1, &layer.opacity)) {
                setError(error, "(opacity n)", field.line, field.column);
                return false;
            }
            layer.opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
        } else if (tag == "offset") {
            if (!readInt2(field, layer.offsetX, layer.offsetY)) {
                setError(error, "(offset x y)", field.line, field.column);
                return false;
            }
        } else if (tag == "repeat") {
            if (!readFlag(field, layer.repeat)) {
                setError(error, "(repeat) or (repeat 0|1)", field.line, field.column);
                return false;
            }
        } else if (tag == "tint") {
            if (!readVec4(field, layer.tint)) {
                setError(error, "(tint r g b a)", field.line, field.column);
                return false;
            }
        } else if (tag == "mask") {
            if (!readRef(field, layer.mask)) {
                setError(error, "(mask \"node-id\")", field.line, field.column);
                return false;
            }
        } else {
            setError(error, "unknown layer field '" + tag + "'", field.line, field.column);
            return false;
        }
    }
    return true;
}

bool parseNodeForm(const Sexpr& form, TextureGraphNode& node, std::string* error) {
    if (form.list.size() < 3 || !form.list[1].isString() || form.list[1].text.empty() ||
        form.list[2].kind != SexprKind::Atom) {
        setError(error, "(node \"id\" kind ...)", form.line, form.column);
        return false;
    }
    node = {};
    node.id = form.list[1].text;
    if (!textureGraphNodeKindFromName(form.list[2].text, node.kind)) {
        setError(error, "unknown node kind '" + form.list[2].text + "'", form.list[2].line, form.list[2].column);
        return false;
    }
    if (node.kind == TextureGraphNodeKind::Stack || node.kind == TextureGraphNodeKind::Transform) {
        node.width = 0;
        node.height = 0;
    }
    for (std::size_t i = 3; i < form.list.size(); ++i) {
        const Sexpr& field = form.list[i];
        if (!field.isList() || field.list.empty() || field.list[0].kind != SexprKind::Atom) {
            setError(error, "node field expected", field.line, field.column);
            return false;
        }
        const std::string& tag = field.list[0].text;
        bool ok = true;
        const char* usage = nullptr;
        if (tag == "texture") {
            ok = readRef(field, node.texture);
            usage = "(texture \"path\")";
        } else if (tag == "input") {
            ok = readRef(field, node.input);
            usage = "(input \"node-id\")";
        } else if (tag == "color") {
            ok = readVec4(field, node.color);
            usage = "(color r g b a)";
        } else if (tag == "color-b") {
            ok = readVec4(field, node.colorB);
            usage = "(color-b r g b a)";
        } else if (tag == "size") {
            ok = readInt2(field, node.width, node.height) && node.width >= 0 && node.height >= 0;
            usage = "(size w h)";
        } else if (tag == "cells") {
            ok = readInt2(field, node.cellsX, node.cellsY) && node.cellsX >= 1 && node.cellsY >= 1;
            usage = "(cells nx ny)";
        } else if (tag == "angle") {
            ok = readNumbers(field, 1, &node.angle);
            usage = "(angle degrees)";
        } else if (tag == "scale") {
            if (node.kind == TextureGraphNodeKind::Transform) {
                float v[2] = {1.0f, 1.0f};
                ok = readNumbers(field, 2, v) && v[0] != 0.0f && v[1] != 0.0f;
                node.scaleX = v[0];
                node.scaleY = v[1];
                usage = "(scale sx sy)";
            } else {
                ok = readNumbers(field, 1, &node.scale) && node.scale > 0.0f;
                usage = "(scale n)";
            }
        } else if (tag == "seed") {
            ok = readInt(field, node.seed);
            usage = "(seed n)";
        } else if (tag == "type") {
            ok = field.list.size() == 2 && field.list[1].kind == SexprKind::Atom &&
                textureGraphNoiseTypeFromName(field.list[1].text, node.noiseType);
            usage = "(type value|perlin|worley)";
        } else if (tag == "octaves") {
            ok = readInt(field, node.octaves) && node.octaves >= 1 && node.octaves <= 8;
            usage = "(octaves 1..8)";
        } else if (tag == "persistence") {
            ok = readNumbers(field, 1, &node.persistence);
            usage = "(persistence n)";
        } else if (tag == "lacunarity") {
            ok = readNumbers(field, 1, &node.lacunarity) && node.lacunarity > 0.0f;
            usage = "(lacunarity n)";
        } else if (tag == "brick") {
            ok = readInt2(field, node.brickWidth, node.brickHeight) && node.brickWidth >= 1 && node.brickHeight >= 1;
            usage = "(brick w h)";
        } else if (tag == "mortar") {
            ok = readInt(field, node.mortar) && node.mortar >= 0;
            usage = "(mortar px)";
        } else if (tag == "row-offset") {
            ok = readNumbers(field, 1, &node.rowOffset);
            usage = "(row-offset fraction)";
        } else if (tag == "stripe") {
            ok = readInt(field, node.stripe) && node.stripe >= 1;
            usage = "(stripe px)";
        } else if (tag == "flip") {
            int h = 0;
            int v = 0;
            ok = readInt2(field, h, v);
            node.flipH = h != 0;
            node.flipV = v != 0;
            usage = "(flip h v)";
        } else if (tag == "in-black") {
            ok = readNumbers(field, 1, &node.inBlack);
            usage = "(in-black n)";
        } else if (tag == "in-white") {
            ok = readNumbers(field, 1, &node.inWhite);
            usage = "(in-white n)";
        } else if (tag == "gamma") {
            ok = readNumbers(field, 1, &node.gamma) && node.gamma > 0.0f;
            usage = "(gamma n)";
        } else if (tag == "out-black") {
            ok = readNumbers(field, 1, &node.outBlack);
            usage = "(out-black n)";
        } else if (tag == "out-white") {
            ok = readNumbers(field, 1, &node.outWhite);
            usage = "(out-white n)";
        } else if (tag == "hue") {
            ok = readNumbers(field, 1, &node.hue);
            usage = "(hue degrees)";
        } else if (tag == "saturation") {
            ok = readNumbers(field, 1, &node.saturation);
            usage = "(saturation n)";
        } else if (tag == "value") {
            ok = readNumbers(field, 1, &node.value);
            usage = "(value n)";
        } else if (tag == "radius") {
            ok = readNumbers(field, 1, &node.radius) && node.radius >= 0.0f;
            usage = "(radius px)";
        } else if (tag == "amount") {
            ok = readNumbers(field, 1, &node.amount);
            usage = "(amount n)";
        } else if (tag == "stop") {
            float v[5] = {};
            ok = readNumbers(field, 5, v);
            usage = "(stop position r g b a)";
            if (ok) {
                node.stops.push_back({std::clamp(v[0], 0.0f, 1.0f), {v[1], v[2], v[3], v[4]}});
            }
        } else if (tag == "layer") {
            TextureGraphLayer layer{};
            if (!parseLayerForm(field, layer, error)) {
                return false;
            }
            node.layers.push_back(std::move(layer));
        } else {
            setError(error, "unknown node field '" + tag + "'", field.line, field.column);
            return false;
        }
        if (!ok) {
            setError(error, usage, field.line, field.column);
            return false;
        }
    }
    std::stable_sort(node.stops.begin(), node.stops.end(), [](const TextureGraphStop& a, const TextureGraphStop& b) {
        return a.position < b.position;
    });
    return true;
}

std::string fmtNum(float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    return buf;
}

std::string fmtVec4(const Vector4& v) {
    return fmtNum(v.x) + " " + fmtNum(v.y) + " " + fmtNum(v.z) + " " + fmtNum(v.w);
}

std::string escapeString(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

Color toColor(const Vector4& v) {
    auto channel = [](float f) {
        return static_cast<unsigned char>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return {channel(v.x), channel(v.y), channel(v.z), channel(v.w)};
}

Vector4 toVec4(Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

float luminance(const Vector4& c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

Vector4 lerp4(const Vector4& a, const Vector4& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

int wrapIndex(int i, int n) {
    return ((i % n) + n) % n;
}

Image blankImage(int w, int h) {
    Image out = GenImageColor(w, h, Color{0, 0, 0, 0});
    ImageFormat(&out, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return out;
}

std::vector<Vector4> toFloatBuffer(const Image& image) {
    const Color* pixels = static_cast<const Color*>(image.data);
    std::vector<Vector4> out(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = toVec4(pixels[i]);
    }
    return out;
}

Image fromFloatBuffer(const std::vector<Vector4>& buffer, int w, int h) {
    Image out = blankImage(w, h);
    Color* pixels = static_cast<Color*>(out.data);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        pixels[i] = toColor(buffer[i]);
    }
    return out;
}

std::uint32_t hashLattice(int x, int y, int seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
        static_cast<std::uint32_t>(seed) * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

float hashUnit(int x, int y, int seed) {
    return static_cast<float>(hashLattice(x, y, seed) & 0xFFFFFFu) / 16777216.0f;
}

float smoothQuintic(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float valueNoise(float u, float v, int period, int seed) {
    const int ix = static_cast<int>(std::floor(u));
    const int iy = static_cast<int>(std::floor(v));
    const float fx = smoothQuintic(u - static_cast<float>(ix));
    const float fy = smoothQuintic(v - static_cast<float>(iy));
    const int x0 = wrapIndex(ix, period);
    const int x1 = wrapIndex(ix + 1, period);
    const int y0 = wrapIndex(iy, period);
    const int y1 = wrapIndex(iy + 1, period);
    const float a = hashUnit(x0, y0, seed);
    const float b = hashUnit(x1, y0, seed);
    const float c = hashUnit(x0, y1, seed);
    const float d = hashUnit(x1, y1, seed);
    const float top = a + (b - a) * fx;
    const float bottom = c + (d - c) * fx;
    return top + (bottom - top) * fy;
}

float gradientDot(int ix, int iy, int seed, float dx, float dy) {
    const float angle = hashUnit(ix, iy, seed) * 2.0f * kPi;
    return std::cos(angle) * dx + std::sin(angle) * dy;
}

float perlinNoise(float u, float v, int period, int seed) {
    const int ix = static_cast<int>(std::floor(u));
    const int iy = static_cast<int>(std::floor(v));
    const float dx = u - static_cast<float>(ix);
    const float dy = v - static_cast<float>(iy);
    const float fx = smoothQuintic(dx);
    const float fy = smoothQuintic(dy);
    const int x0 = wrapIndex(ix, period);
    const int x1 = wrapIndex(ix + 1, period);
    const int y0 = wrapIndex(iy, period);
    const int y1 = wrapIndex(iy + 1, period);
    const float a = gradientDot(x0, y0, seed, dx, dy);
    const float b = gradientDot(x1, y0, seed, dx - 1.0f, dy);
    const float c = gradientDot(x0, y1, seed, dx, dy - 1.0f);
    const float d = gradientDot(x1, y1, seed, dx - 1.0f, dy - 1.0f);
    const float top = a + (b - a) * fx;
    const float bottom = c + (d - c) * fx;
    const float n = top + (bottom - top) * fy;
    return std::clamp(n * 0.75f + 0.5f, 0.0f, 1.0f);
}

float worleyNoise(float u, float v, int period, int seed) {
    const int ix = static_cast<int>(std::floor(u));
    const int iy = static_cast<int>(std::floor(v));
    float best = 4.0f;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int cx = ix + ox;
            const int cy = iy + oy;
            const int wx = wrapIndex(cx, period);
            const int wy = wrapIndex(cy, period);
            const float px = static_cast<float>(cx) + hashUnit(wx, wy, seed);
            const float py = static_cast<float>(cy) + hashUnit(wx, wy, seed + 7919);
            const float dx = px - u;
            const float dy = py - v;
            best = std::min(best, dx * dx + dy * dy);
        }
    }
    return std::clamp(std::sqrt(best), 0.0f, 1.0f);
}

float sampleNoise(const TextureGraphNode& node, float nx, float ny) {
    float sum = 0.0f;
    float ampSum = 0.0f;
    float amp = 1.0f;
    float freq = node.scale;
    for (int octave = 0; octave < std::max(1, node.octaves); ++octave) {
        const int period = std::max(1, static_cast<int>(std::lround(freq)));
        const float u = nx * static_cast<float>(period);
        const float v = ny * static_cast<float>(period);
        float n = 0.0f;
        switch (node.noiseType) {
        case TextureGraphNoiseType::Value: n = valueNoise(u, v, period, node.seed + octave * 101); break;
        case TextureGraphNoiseType::Perlin: n = perlinNoise(u, v, period, node.seed + octave * 101); break;
        case TextureGraphNoiseType::Worley: n = worleyNoise(u, v, period, node.seed + octave * 101); break;
        }
        sum += n * amp;
        ampSum += amp;
        amp *= node.persistence;
        freq *= node.lacunarity;
    }
    return ampSum > 0.0f ? sum / ampSum : 0.0f;
}

bool generateImage(const TextureGraphNode& node, Image& out, std::string& error) {
    const int w = std::max(1, node.width);
    const int h = std::max(1, node.height);
    out = blankImage(w, h);
    Color* pixels = static_cast<Color*>(out.data);
    const Color a = toColor(node.color);
    const Color b = toColor(node.colorB);
    switch (node.kind) {
    case TextureGraphNodeKind::Color:
        for (int i = 0; i < w * h; ++i) {
            pixels[i] = a;
        }
        return true;
    case TextureGraphNodeKind::Checker: {
        const float cellW = static_cast<float>(w) / static_cast<float>(node.cellsX);
        const float cellH = static_cast<float>(h) / static_cast<float>(node.cellsY);
        for (int y = 0; y < h; ++y) {
            const int cy = static_cast<int>(static_cast<float>(y) / cellH);
            for (int x = 0; x < w; ++x) {
                const int cx = static_cast<int>(static_cast<float>(x) / cellW);
                pixels[y * w + x] = ((cx + cy) & 1) == 0 ? a : b;
            }
        }
        return true;
    }
    case TextureGraphNodeKind::Gradient: {
        const float rad = node.angle * kPi / 180.0f;
        const float dirX = std::sin(rad);
        const float dirY = std::cos(rad);
        const float extent = std::fabs(dirX) + std::fabs(dirY);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 0.5f;
                const float py = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) - 0.5f;
                const float t = std::clamp((px * dirX + py * dirY) / extent + 0.5f, 0.0f, 1.0f);
                pixels[y * w + x] = toColor(lerp4(node.color, node.colorB, t));
            }
        }
        return true;
    }
    case TextureGraphNodeKind::Noise:
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float n = sampleNoise(node, (static_cast<float>(x) + 0.5f) / static_cast<float>(w), (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
                pixels[y * w + x] = toColor(lerp4(node.colorB, node.color, n));
            }
        }
        return true;
    case TextureGraphNodeKind::Bricks: {
        const int bw = node.brickWidth;
        const int bh = node.brickHeight;
        for (int y = 0; y < h; ++y) {
            const int row = static_cast<int>(std::floor(static_cast<float>(y) / static_cast<float>(bh)));
            const int shift = static_cast<int>(std::lround(static_cast<float>(row) * node.rowOffset * static_cast<float>(bw)));
            const int ly = wrapIndex(y, bh);
            for (int x = 0; x < w; ++x) {
                const int lx = wrapIndex(x + shift, bw);
                const bool mortar = lx < node.mortar || ly < node.mortar;
                pixels[y * w + x] = mortar ? b : a;
            }
        }
        return true;
    }
    case TextureGraphNodeKind::Stripes: {
        const float rad = node.angle * kPi / 180.0f;
        const float dirX = std::sin(rad);
        const float dirY = std::cos(rad);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float t = (static_cast<float>(x) * dirX + static_cast<float>(y) * dirY) / static_cast<float>(node.stripe);
                const int band = static_cast<int>(std::floor(t));
                pixels[y * w + x] = (band & 1) == 0 ? a : b;
            }
        }
        return true;
    }
    default:
        break;
    }
    UnloadImage(out);
    out = {};
    error = "node '" + node.id + "' is not a generator";
    return false;
}

Vector4 rgbToHsv(const Vector4& c) {
    const float maxC = std::max({c.x, c.y, c.z});
    const float minC = std::min({c.x, c.y, c.z});
    const float delta = maxC - minC;
    float hue = 0.0f;
    if (delta > 1e-6f) {
        if (maxC == c.x) {
            hue = std::fmod((c.y - c.z) / delta, 6.0f);
        } else if (maxC == c.y) {
            hue = (c.z - c.x) / delta + 2.0f;
        } else {
            hue = (c.x - c.y) / delta + 4.0f;
        }
        hue *= 60.0f;
        if (hue < 0.0f) {
            hue += 360.0f;
        }
    }
    const float sat = maxC > 1e-6f ? delta / maxC : 0.0f;
    return {hue, sat, maxC, c.w};
}

Vector4 hsvToRgb(const Vector4& hsv) {
    const float h = std::fmod(std::fmod(hsv.x, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const float s = std::clamp(hsv.y, 0.0f, 1.0f);
    const float v = std::clamp(hsv.z, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (h < 1.0f) { r = c; g = x; }
    else if (h < 2.0f) { r = x; g = c; }
    else if (h < 3.0f) { g = c; b = x; }
    else if (h < 4.0f) { g = x; b = c; }
    else if (h < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }
    return {r + m, g + m, b + m, hsv.w};
}

std::vector<Vector4> boxBlur(const std::vector<Vector4>& src, int w, int h, int radius) {
    if (radius <= 0) {
        return src;
    }
    std::vector<Vector4> tmp(src.size());
    std::vector<Vector4> out(src.size());
    const float norm = 1.0f / static_cast<float>(radius * 2 + 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Vector4 acc{};
            for (int k = -radius; k <= radius; ++k) {
                const Vector4& s = src[static_cast<std::size_t>(y * w + wrapIndex(x + k, w))];
                acc.x += s.x; acc.y += s.y; acc.z += s.z; acc.w += s.w;
            }
            tmp[static_cast<std::size_t>(y * w + x)] = {acc.x * norm, acc.y * norm, acc.z * norm, acc.w * norm};
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Vector4 acc{};
            for (int k = -radius; k <= radius; ++k) {
                const Vector4& s = tmp[static_cast<std::size_t>(wrapIndex(y + k, h) * w + x)];
                acc.x += s.x; acc.y += s.y; acc.z += s.z; acc.w += s.w;
            }
            out[static_cast<std::size_t>(y * w + x)] = {acc.x * norm, acc.y * norm, acc.z * norm, acc.w * norm};
        }
    }
    return out;
}

bool applyUnary(const TextureGraphNode& node, const Image& in, Image& out, std::string& error) {
    const int w = in.width;
    const int h = in.height;
    std::vector<Vector4> src = toFloatBuffer(in);
    switch (node.kind) {
    case TextureGraphNodeKind::GradientMap: {
        if (node.stops.empty()) {
            error = "gradient-map '" + node.id + "' has no stops";
            return false;
        }
        for (Vector4& p : src) {
            const float t = std::clamp(luminance(p), 0.0f, 1.0f);
            Vector4 result = node.stops.front().color;
            if (t >= node.stops.back().position) {
                result = node.stops.back().color;
            } else {
                for (std::size_t i = 0; i + 1 < node.stops.size(); ++i) {
                    const TextureGraphStop& s0 = node.stops[i];
                    const TextureGraphStop& s1 = node.stops[i + 1];
                    if (t >= s0.position && t < s1.position) {
                        const float span = s1.position - s0.position;
                        result = lerp4(s0.color, s1.color, span > 1e-6f ? (t - s0.position) / span : 0.0f);
                        break;
                    }
                }
            }
            p = result;
        }
        out = fromFloatBuffer(src, w, h);
        return true;
    }
    case TextureGraphNodeKind::Transform: {
        const int ow = node.width > 0 ? node.width : w;
        const int oh = node.height > 0 ? node.height : h;
        std::vector<Vector4> dst(static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh));
        const float rad = -node.angle * kPi / 180.0f;
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);
        const float cx = static_cast<float>(ow) * 0.5f;
        const float cy = static_cast<float>(oh) * 0.5f;
        for (int y = 0; y < oh; ++y) {
            for (int x = 0; x < ow; ++x) {
                const float px = static_cast<float>(x) + 0.5f - cx;
                const float py = static_cast<float>(y) + 0.5f - cy;
                const float rx = px * cosA - py * sinA;
                const float ry = px * sinA + py * cosA;
                float sx = rx / node.scaleX + static_cast<float>(w) * 0.5f;
                float sy = ry / node.scaleY + static_cast<float>(h) * 0.5f;
                int ix = wrapIndex(static_cast<int>(std::floor(sx)), w);
                int iy = wrapIndex(static_cast<int>(std::floor(sy)), h);
                if (node.flipH) {
                    ix = w - 1 - ix;
                }
                if (node.flipV) {
                    iy = h - 1 - iy;
                }
                dst[static_cast<std::size_t>(y * ow + x)] = src[static_cast<std::size_t>(iy * w + ix)];
            }
        }
        out = fromFloatBuffer(dst, ow, oh);
        return true;
    }
    case TextureGraphNodeKind::Levels: {
        const float inSpan = node.inWhite - node.inBlack;
        for (Vector4& p : src) {
            float* channels[3] = {&p.x, &p.y, &p.z};
            for (float* c : channels) {
                float t = std::fabs(inSpan) > 1e-6f ? (*c - node.inBlack) / inSpan : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                t = std::pow(t, 1.0f / node.gamma);
                *c = std::clamp(node.outBlack + t * (node.outWhite - node.outBlack), 0.0f, 1.0f);
            }
        }
        out = fromFloatBuffer(src, w, h);
        return true;
    }
    case TextureGraphNodeKind::Hsv:
        for (Vector4& p : src) {
            Vector4 hsv = rgbToHsv(p);
            hsv.x += node.hue;
            hsv.y *= node.saturation;
            hsv.z *= node.value;
            p = hsvToRgb(hsv);
        }
        out = fromFloatBuffer(src, w, h);
        return true;
    case TextureGraphNodeKind::Invert:
        for (Vector4& p : src) {
            p.x = 1.0f - p.x;
            p.y = 1.0f - p.y;
            p.z = 1.0f - p.z;
        }
        out = fromFloatBuffer(src, w, h);
        return true;
    case TextureGraphNodeKind::Blur:
        out = fromFloatBuffer(boxBlur(src, w, h, static_cast<int>(std::ceil(node.radius))), w, h);
        return true;
    case TextureGraphNodeKind::Sharpen: {
        const std::vector<Vector4> soft = boxBlur(src, w, h, 1);
        for (std::size_t i = 0; i < src.size(); ++i) {
            src[i].x = std::clamp(src[i].x + node.amount * (src[i].x - soft[i].x), 0.0f, 1.0f);
            src[i].y = std::clamp(src[i].y + node.amount * (src[i].y - soft[i].y), 0.0f, 1.0f);
            src[i].z = std::clamp(src[i].z + node.amount * (src[i].z - soft[i].z), 0.0f, 1.0f);
        }
        out = fromFloatBuffer(src, w, h);
        return true;
    }
    case TextureGraphNodeKind::Edges: {
        std::vector<float> lum(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) {
            lum[i] = luminance(src[i]);
        }
        auto at = [&](int x, int y) { return lum[static_cast<std::size_t>(wrapIndex(y, h) * w + wrapIndex(x, w))]; };
        std::vector<Vector4> dst(src.size());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float gx = -at(x - 1, y - 1) - 2.0f * at(x - 1, y) - at(x - 1, y + 1) + at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1);
                const float gy = -at(x - 1, y - 1) - 2.0f * at(x, y - 1) - at(x + 1, y - 1) + at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1);
                const float mag = std::clamp(std::sqrt(gx * gx + gy * gy) * node.amount, 0.0f, 1.0f);
                dst[static_cast<std::size_t>(y * w + x)] = {mag, mag, mag, 1.0f};
            }
        }
        out = fromFloatBuffer(dst, w, h);
        return true;
    }
    default:
        error = "node '" + node.id + "' is not a filter";
        return false;
    }
}

struct EvalContext {
    const TextureGraph& graph;
    const TextureGraphImageResolver& resolveImage;
    std::unordered_map<std::string, Image> memo;
    std::unordered_set<std::string> visiting;
    std::string error;

    ~EvalContext() {
        for (auto& [_, image] : memo) {
            UnloadImage(image);
        }
    }

    const Image* evaluate(const std::string& nodeId);
};

float blendChannel(TextureGraphBlend blend, float dst, float src) {
    switch (blend) {
    case TextureGraphBlend::Multiply: return dst * src;
    case TextureGraphBlend::Add: return std::min(1.0f, dst + src);
    case TextureGraphBlend::Screen: return 1.0f - (1.0f - dst) * (1.0f - src);
    case TextureGraphBlend::Normal:
    default: return src;
    }
}

bool compositeStack(EvalContext& ctx, const TextureGraphNode& node, Image& out) {
    if (node.layers.empty()) {
        ctx.error = "stack '" + node.id + "' has no layers";
        return false;
    }
    std::vector<const Image*> sources;
    std::vector<const Image*> masks;
    sources.reserve(node.layers.size());
    masks.reserve(node.layers.size());
    for (const TextureGraphLayer& layer : node.layers) {
        const Image* source = ctx.evaluate(layer.source);
        if (source == nullptr) {
            return false;
        }
        sources.push_back(source);
        const Image* mask = nullptr;
        if (!layer.mask.empty()) {
            mask = ctx.evaluate(layer.mask);
            if (mask == nullptr) {
                return false;
            }
        }
        masks.push_back(mask);
    }

    const int w = node.width > 0 ? node.width : sources[0]->width;
    const int h = node.height > 0 ? node.height : sources[0]->height;
    out = blankImage(w, h);
    Color* canvas = static_cast<Color*>(out.data);

    for (std::size_t li = 0; li < node.layers.size(); ++li) {
        const TextureGraphLayer& layer = node.layers[li];
        const Image& src = *sources[li];
        const Image* mask = masks[li];
        const Color* srcPixels = static_cast<const Color*>(src.data);
        const Color* maskPixels = mask != nullptr ? static_cast<const Color*>(mask->data) : nullptr;
        for (int y = 0; y < h; ++y) {
            int sy = y - layer.offsetY;
            if (layer.repeat) {
                sy = wrapIndex(sy, src.height);
            } else if (sy < 0 || sy >= src.height) {
                continue;
            }
            for (int x = 0; x < w; ++x) {
                int sx = x - layer.offsetX;
                if (layer.repeat) {
                    sx = wrapIndex(sx, src.width);
                } else if (sx < 0 || sx >= src.width) {
                    continue;
                }
                const Vector4 s = toVec4(srcPixels[sy * src.width + sx]);
                const Vector4 d = toVec4(canvas[y * w + x]);
                float srcA = s.w * layer.tint.w * layer.opacity;
                if (maskPixels != nullptr) {
                    const Vector4 m = toVec4(maskPixels[wrapIndex(y, mask->height) * mask->width + wrapIndex(x, mask->width)]);
                    srcA *= luminance(m) * m.w;
                }
                if (srcA <= 0.0f) {
                    continue;
                }
                const float sr = s.x * layer.tint.x;
                const float sg = s.y * layer.tint.y;
                const float sb = s.z * layer.tint.z;
                const float br = (1.0f - d.w) * sr + d.w * blendChannel(layer.blend, d.x, sr);
                const float bg = (1.0f - d.w) * sg + d.w * blendChannel(layer.blend, d.y, sg);
                const float bb = (1.0f - d.w) * sb + d.w * blendChannel(layer.blend, d.z, sb);
                const float outA = srcA + d.w * (1.0f - srcA);
                Vector4 result{0.0f, 0.0f, 0.0f, outA};
                if (outA > 0.0f) {
                    const float dw = d.w * (1.0f - srcA);
                    result.x = (br * srcA + d.x * dw) / outA;
                    result.y = (bg * srcA + d.y * dw) / outA;
                    result.z = (bb * srcA + d.z * dw) / outA;
                }
                canvas[y * w + x] = toColor(result);
            }
        }
    }
    return true;
}

const Image* EvalContext::evaluate(const std::string& nodeId) {
    const auto existing = memo.find(nodeId);
    if (existing != memo.end()) {
        return &existing->second;
    }
    const TextureGraphNode* node = graph.findNode(nodeId);
    if (node == nullptr) {
        error = "unknown node '" + nodeId + "'";
        return nullptr;
    }
    if (!visiting.insert(nodeId).second) {
        error = "cycle through node '" + nodeId + "'";
        return nullptr;
    }

    Image image{};
    bool ok = false;
    if (node->kind == TextureGraphNodeKind::Image) {
        if (node->texture.empty()) {
            error = "image node '" + nodeId + "' has no texture";
        } else if (!resolveImage || !resolveImage(node->texture, image) || image.data == nullptr) {
            error = "image node '" + nodeId + "' could not load texture '" + node->texture + "'";
        } else {
            ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ok = true;
        }
    } else if (node->kind == TextureGraphNodeKind::Stack) {
        ok = compositeStack(*this, *node, image);
    } else if (textureGraphKindHasInput(node->kind)) {
        if (node->input.empty()) {
            error = "node '" + nodeId + "' has no input";
        } else {
            const Image* in = evaluate(node->input);
            if (in != nullptr) {
                ok = applyUnary(*node, *in, image, error);
            }
        }
    } else {
        ok = generateImage(*node, image, error);
    }
    visiting.erase(nodeId);
    if (!ok) {
        if (image.data != nullptr) {
            UnloadImage(image);
        }
        return nullptr;
    }
    return &memo.emplace(nodeId, image).first->second;
}

void writeSize(std::ostringstream& out, const TextureGraphNode& node) {
    out << "    (size " << node.width << " " << node.height << ")\n";
}

void writeColors(std::ostringstream& out, const TextureGraphNode& node) {
    out << "    (color " << fmtVec4(node.color) << ")\n";
    out << "    (color-b " << fmtVec4(node.colorB) << ")\n";
}

void writeInput(std::ostringstream& out, const TextureGraphNode& node) {
    out << "    (input " << escapeString(node.input) << ")\n";
}

} // namespace

const TextureGraphNode* TextureGraph::findNode(std::string_view id) const {
    for (const TextureGraphNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

TextureGraphNode* TextureGraph::findNode(std::string_view id) {
    for (TextureGraphNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

const TextureGraphOutput* TextureGraph::findOutput(std::string_view name) const {
    for (const TextureGraphOutput& output : outputs) {
        if (output.name == name) {
            return &output;
        }
    }
    return nullptr;
}

const char* textureGraphNodeKindName(TextureGraphNodeKind kind) {
    for (const KindName& entry : kKindNames) {
        if (entry.kind == kind) {
            return entry.name;
        }
    }
    return "image";
}

bool textureGraphNodeKindFromName(std::string_view name, TextureGraphNodeKind& out) {
    for (const KindName& entry : kKindNames) {
        if (name == entry.name) {
            out = entry.kind;
            return true;
        }
    }
    return false;
}

const char* textureGraphBlendName(TextureGraphBlend blend) {
    for (const BlendName& entry : kBlendNames) {
        if (entry.blend == blend) {
            return entry.name;
        }
    }
    return "normal";
}

bool textureGraphBlendFromName(std::string_view name, TextureGraphBlend& out) {
    for (const BlendName& entry : kBlendNames) {
        if (name == entry.name) {
            out = entry.blend;
            return true;
        }
    }
    return false;
}

const char* textureGraphNoiseTypeName(TextureGraphNoiseType type) {
    for (const NoiseName& entry : kNoiseNames) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    return "perlin";
}

bool textureGraphNoiseTypeFromName(std::string_view name, TextureGraphNoiseType& out) {
    for (const NoiseName& entry : kNoiseNames) {
        if (name == entry.name) {
            out = entry.type;
            return true;
        }
    }
    return false;
}

bool textureGraphKindHasInput(TextureGraphNodeKind kind) {
    switch (kind) {
    case TextureGraphNodeKind::GradientMap:
    case TextureGraphNodeKind::Transform:
    case TextureGraphNodeKind::Levels:
    case TextureGraphNodeKind::Hsv:
    case TextureGraphNodeKind::Invert:
    case TextureGraphNodeKind::Blur:
    case TextureGraphNodeKind::Sharpen:
    case TextureGraphNodeKind::Edges:
        return true;
    default:
        return false;
    }
}

std::vector<std::string> textureGraphNodeReferences(const TextureGraphNode& node) {
    std::vector<std::string> refs;
    if (textureGraphKindHasInput(node.kind) && !node.input.empty()) {
        refs.push_back(node.input);
    }
    for (const TextureGraphLayer& layer : node.layers) {
        refs.push_back(layer.source);
        if (!layer.mask.empty()) {
            refs.push_back(layer.mask);
        }
    }
    return refs;
}

bool parseTextureGraph(std::string_view source, TextureGraph& graph, std::string* error) {
    graph = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        setError(error, parsed.error.message, parsed.error.line, parsed.error.column);
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("texture-graph")) {
        setError(error, "expected a single (texture-graph ...) form", 1, 1);
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        const Sexpr& form = root.list[i];
        if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
            setError(error, "expected (node ...) or (output ...)", form.line, form.column);
            return false;
        }
        const std::string& tag = form.list[0].text;
        if (tag == "node") {
            TextureGraphNode node{};
            if (!parseNodeForm(form, node, error)) {
                return false;
            }
            graph.nodes.push_back(std::move(node));
        } else if (tag == "output") {
            if (form.list.size() != 3 || !form.list[1].isString() || !form.list[2].isString()) {
                setError(error, "(output \"name\" \"node-id\")", form.line, form.column);
                return false;
            }
            graph.outputs.push_back({form.list[1].text, form.list[2].text});
        } else {
            setError(error, "unknown texture-graph field '" + tag + "'", form.line, form.column);
            return false;
        }
    }

    std::string validation;
    if (!validateTextureGraph(graph, validation)) {
        if (error != nullptr) {
            *error = validation;
        }
        return false;
    }
    return true;
}

std::string serializeTextureGraph(const TextureGraph& graph) {
    std::ostringstream out;
    out << "(texture-graph\n";
    for (const TextureGraphNode& node : graph.nodes) {
        out << "  (node " << escapeString(node.id) << " " << textureGraphNodeKindName(node.kind) << "\n";
        switch (node.kind) {
        case TextureGraphNodeKind::Image:
            out << "    (texture " << escapeString(node.texture) << ")\n";
            break;
        case TextureGraphNodeKind::Color:
            writeSize(out, node);
            out << "    (color " << fmtVec4(node.color) << ")\n";
            break;
        case TextureGraphNodeKind::Checker:
            writeSize(out, node);
            out << "    (cells " << node.cellsX << " " << node.cellsY << ")\n";
            writeColors(out, node);
            break;
        case TextureGraphNodeKind::Gradient:
            writeSize(out, node);
            out << "    (angle " << fmtNum(node.angle) << ")\n";
            writeColors(out, node);
            break;
        case TextureGraphNodeKind::Noise:
            writeSize(out, node);
            out << "    (type " << textureGraphNoiseTypeName(node.noiseType) << ")\n";
            out << "    (scale " << fmtNum(node.scale) << ")\n";
            out << "    (octaves " << node.octaves << ")\n";
            out << "    (persistence " << fmtNum(node.persistence) << ")\n";
            out << "    (lacunarity " << fmtNum(node.lacunarity) << ")\n";
            out << "    (seed " << node.seed << ")\n";
            writeColors(out, node);
            break;
        case TextureGraphNodeKind::Bricks:
            writeSize(out, node);
            out << "    (brick " << node.brickWidth << " " << node.brickHeight << ")\n";
            out << "    (mortar " << node.mortar << ")\n";
            out << "    (row-offset " << fmtNum(node.rowOffset) << ")\n";
            writeColors(out, node);
            break;
        case TextureGraphNodeKind::Stripes:
            writeSize(out, node);
            out << "    (stripe " << node.stripe << ")\n";
            out << "    (angle " << fmtNum(node.angle) << ")\n";
            writeColors(out, node);
            break;
        case TextureGraphNodeKind::GradientMap:
            writeInput(out, node);
            for (const TextureGraphStop& stop : node.stops) {
                out << "    (stop " << fmtNum(stop.position) << " " << fmtVec4(stop.color) << ")\n";
            }
            break;
        case TextureGraphNodeKind::Transform:
            writeInput(out, node);
            if (node.width > 0 || node.height > 0) {
                writeSize(out, node);
            }
            out << "    (scale " << fmtNum(node.scaleX) << " " << fmtNum(node.scaleY) << ")\n";
            out << "    (angle " << fmtNum(node.angle) << ")\n";
            out << "    (flip " << (node.flipH ? 1 : 0) << " " << (node.flipV ? 1 : 0) << ")\n";
            break;
        case TextureGraphNodeKind::Levels:
            writeInput(out, node);
            out << "    (in-black " << fmtNum(node.inBlack) << ")\n";
            out << "    (in-white " << fmtNum(node.inWhite) << ")\n";
            out << "    (gamma " << fmtNum(node.gamma) << ")\n";
            out << "    (out-black " << fmtNum(node.outBlack) << ")\n";
            out << "    (out-white " << fmtNum(node.outWhite) << ")\n";
            break;
        case TextureGraphNodeKind::Hsv:
            writeInput(out, node);
            out << "    (hue " << fmtNum(node.hue) << ")\n";
            out << "    (saturation " << fmtNum(node.saturation) << ")\n";
            out << "    (value " << fmtNum(node.value) << ")\n";
            break;
        case TextureGraphNodeKind::Invert:
            writeInput(out, node);
            break;
        case TextureGraphNodeKind::Blur:
            writeInput(out, node);
            out << "    (radius " << fmtNum(node.radius) << ")\n";
            break;
        case TextureGraphNodeKind::Sharpen:
        case TextureGraphNodeKind::Edges:
            writeInput(out, node);
            out << "    (amount " << fmtNum(node.amount) << ")\n";
            break;
        case TextureGraphNodeKind::Stack:
            if (node.width > 0 || node.height > 0) {
                writeSize(out, node);
            }
            for (const TextureGraphLayer& layer : node.layers) {
                out << "    (layer " << escapeString(layer.source);
                if (layer.blend != TextureGraphBlend::Normal) {
                    out << " (blend " << textureGraphBlendName(layer.blend) << ")";
                }
                if (layer.opacity != 1.0f) {
                    out << " (opacity " << fmtNum(layer.opacity) << ")";
                }
                if (layer.offsetX != 0 || layer.offsetY != 0) {
                    out << " (offset " << layer.offsetX << " " << layer.offsetY << ")";
                }
                if (layer.repeat) {
                    out << " (repeat 1)";
                }
                if (layer.tint.x != 1.0f || layer.tint.y != 1.0f || layer.tint.z != 1.0f || layer.tint.w != 1.0f) {
                    out << " (tint " << fmtVec4(layer.tint) << ")";
                }
                if (!layer.mask.empty()) {
                    out << " (mask " << escapeString(layer.mask) << ")";
                }
                out << ")\n";
            }
            break;
        }
        out << "  )\n";
    }
    for (const TextureGraphOutput& output : graph.outputs) {
        out << "  (output " << escapeString(output.name) << " " << escapeString(output.source) << ")\n";
    }
    out << ")\n";
    return out.str();
}

bool validateTextureGraph(const TextureGraph& graph, std::string& error) {
    std::unordered_set<std::string> ids;
    for (const TextureGraphNode& node : graph.nodes) {
        if (node.id.empty()) {
            error = "node with empty id";
            return false;
        }
        if (!ids.insert(node.id).second) {
            error = "duplicate node id '" + node.id + "'";
            return false;
        }
    }
    for (const TextureGraphNode& node : graph.nodes) {
        if (textureGraphKindHasInput(node.kind) && node.input.empty()) {
            error = "node '" + node.id + "' needs an input";
            return false;
        }
        for (const TextureGraphLayer& layer : node.layers) {
            if (layer.source.empty()) {
                error = "stack '" + node.id + "' has a layer without a source";
                return false;
            }
        }
        for (const std::string& ref : textureGraphNodeReferences(node)) {
            if (ids.count(ref) == 0) {
                error = "node '" + node.id + "' references unknown node '" + ref + "'";
                return false;
            }
        }
    }
    if (graph.outputs.empty()) {
        error = "graph has no outputs";
        return false;
    }
    std::unordered_set<std::string> outputNames;
    for (const TextureGraphOutput& output : graph.outputs) {
        if (output.name.empty()) {
            error = "output with empty name";
            return false;
        }
        if (!outputNames.insert(output.name).second) {
            error = "duplicate output '" + output.name + "'";
            return false;
        }
        if (ids.count(output.source) == 0) {
            error = "output '" + output.name + "' references unknown node '" + output.source + "'";
            return false;
        }
    }

    std::unordered_map<std::string, int> state;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        const int current = state[id];
        if (current == 1) {
            error = "cycle through node '" + id + "'";
            return false;
        }
        if (current == 2) {
            return true;
        }
        state[id] = 1;
        for (const std::string& ref : textureGraphNodeReferences(*graph.findNode(id))) {
            if (!visit(ref)) {
                return false;
            }
        }
        state[id] = 2;
        return true;
    };
    for (const TextureGraphNode& node : graph.nodes) {
        if (!visit(node.id)) {
            return false;
        }
    }
    return true;
}

bool evaluateTextureGraph(
    const TextureGraph& graph,
    std::string_view outputName,
    const TextureGraphImageResolver& resolveImage,
    Image& out,
    std::string& error) {
    out = {};
    const TextureGraphOutput* output = graph.findOutput(outputName);
    if (output == nullptr) {
        error = "graph has no output '" + std::string(outputName) + "'";
        return false;
    }
    EvalContext ctx{graph, resolveImage, {}, {}, {}};
    const Image* result = ctx.evaluate(output->source);
    if (result == nullptr) {
        error = ctx.error;
        return false;
    }
    out = ImageCopy(*result);
    return out.data != nullptr;
}

}
