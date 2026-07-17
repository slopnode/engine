#include "assets/sprite_loader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slopengine {

namespace {

constexpr int kAtlasPadding = 1;
constexpr int kDefaultAtlasSize = 512;

SpriteHitmask bakeSingleHitmask(const Image& albedo) {
    SpriteHitmask mask{};
    mask.width = albedo.width;
    mask.height = albedo.height;
    if (mask.width <= 0 || mask.height <= 0 || albedo.data == nullptr) {
        return mask;
    }

    const int pixelCount = mask.width * mask.height;
    mask.parts.assign(static_cast<std::size_t>(pixelCount), 0);
    mask.partNames = {"default"};
    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const Color color = GetImageColor(albedo, x, y);
            if (color.a < kSpriteHitmaskAlphaThreshold) {
                continue;
            }
            mask.parts[static_cast<std::size_t>(y * mask.width + x)] = 1;
        }
    }
    return mask;
}

std::uint8_t matchHitPart(
    const Color& color,
    const std::vector<SpriteHitPartDef>& hitParts) {
    for (std::size_t index = 0; index < hitParts.size(); ++index) {
        const SpriteHitPartDef& part = hitParts[index];
        if (color.r == part.r && color.g == part.g && color.b == part.b) {
            return static_cast<std::uint8_t>(index + 1);
        }
    }
    return 0;
}

SpriteHitmask bakeColoredHitmask(
    const Image& hitImage,
    const std::vector<SpriteHitPartDef>& hitParts) {
    SpriteHitmask mask{};
    mask.width = hitImage.width;
    mask.height = hitImage.height;
    if (mask.width <= 0 || mask.height <= 0 || hitImage.data == nullptr || hitParts.empty()) {
        return mask;
    }

    const int pixelCount = mask.width * mask.height;
    mask.parts.assign(static_cast<std::size_t>(pixelCount), 0);
    mask.partNames.reserve(hitParts.size());
    for (const SpriteHitPartDef& part : hitParts) {
        mask.partNames.push_back(part.name);
    }

    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const Color color = GetImageColor(hitImage, x, y);
            if (color.a < kSpriteHitmaskAlphaThreshold) {
                continue;
            }
            mask.parts[static_cast<std::size_t>(y * mask.width + x)] =
                matchHitPart(color, hitParts);
        }
    }
    return mask;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::string> readQuotedField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteStart = line.find('"', prefixPos + prefix.size());
    if (quoteStart == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteEnd = line.find('"', quoteStart + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string{line.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
}

bool readFloats(std::string_view text, std::size_t count, float* out) {
    std::string_view value = trim(text);
    for (std::size_t index = 0; index < count; ++index) {
        value = trim(value);
        if (value.empty()) {
            return false;
        }

        float parsed = 0.0f;
        const auto* begin = value.data();
        const auto* end = value.data() + value.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr == begin) {
            return false;
        }

        out[index] = parsed;
        value.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    }

    return true;
}

bool readInt(std::string_view text, int& out, std::string_view& rest) {
    text = trim(text);
    if (text.empty()) {
        return false;
    }

    int parsed = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return false;
    }

    out = parsed;
    rest = trim(text.substr(static_cast<std::size_t>(result.ptr - begin)));
    return true;
}

bool lineContainsMirror(std::string_view line) {
    std::string lower{line};
    for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return lower.find("mirror") != std::string::npos;
}

std::optional<std::string> readHitMaskPath(std::string_view line) {
    const std::size_t hitPos = line.find("hit ");
    if (hitPos == std::string_view::npos) {
        return std::nullopt;
    }
    return readQuotedField(line.substr(hitPos), "hit ");
}

int nextPowerOfTwo(int value) {
    int power = 1;
    while (power < value) {
        power <<= 1;
    }
    return power;
}

} // namespace

bool parseSpriteAsset(std::string_view source, SpriteAsset& asset) {
    asset = {};
    SpriteFrame* currentFrame = nullptr;

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (line.rfind("(texel-size ", 0) == 0) {
            float texelSize = 64.0f;
            if (!readFloats(line.substr(std::string_view("(texel-size ").size()), 1, &texelSize)) {
                return false;
            }
            asset.pixelsPerMeter = texelSize;
        } else if (auto partName = readQuotedField(line, "(hit-part ")) {
            std::string_view rest = line;
            const std::size_t nameEnd = rest.find('"', rest.find('"') + 1);
            if (nameEnd == std::string_view::npos) {
                return false;
            }
            rest = trim(rest.substr(nameEnd + 1));
            float rgb[3] = {};
            if (!readFloats(rest, 3, rgb)) {
                return false;
            }
            SpriteHitPartDef part{};
            part.name = *partName;
            part.r = static_cast<unsigned char>(std::clamp(rgb[0], 0.0f, 255.0f));
            part.g = static_cast<unsigned char>(std::clamp(rgb[1], 0.0f, 255.0f));
            part.b = static_cast<unsigned char>(std::clamp(rgb[2], 0.0f, 255.0f));
            asset.hitParts.push_back(std::move(part));
        } else if (auto frameId = readQuotedField(line, "(frame ")) {
            asset.frames.push_back(SpriteFrame{});
            currentFrame = &asset.frames.back();
            currentFrame->id = *frameId;
        } else if (line.rfind("(rot ", 0) == 0) {
            if (currentFrame == nullptr) {
                return false;
            }

            std::string_view rest;
            int rotation = 0;
            if (!readInt(line.substr(std::string_view("(rot ").size()), rotation, rest)) {
                return false;
            }
            if (rotation < 0 || rotation >= kSpriteRotationCount) {
                return false;
            }

            const std::size_t quoteStart = rest.find('"');
            if (quoteStart == std::string_view::npos) {
                return false;
            }
            const std::size_t quoteEnd = rest.find('"', quoteStart + 1);
            if (quoteEnd == std::string_view::npos) {
                return false;
            }

            SpriteRotation entry{};
            entry.texturePath = std::string{rest.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
            entry.mirror = lineContainsMirror(line);
            entry.hitMaskPath = readHitMaskPath(line);
            currentFrame->rotations[rotation] = std::move(entry);
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return !asset.frames.empty();
}

SpriteAtlas buildSpriteAtlas(
    const SpriteAsset& asset,
    const std::function<std::optional<std::filesystem::path>(std::string_view)>& resolveTexturePath) {
    SpriteAtlas atlas{};

    std::vector<std::string> uniquePaths;
    std::unordered_set<std::string> seen;
    std::unordered_map<std::string, std::string> textureHitPaths;
    for (const SpriteFrame& frame : asset.frames) {
        for (int rotation = 0; rotation < kSpriteRotationCount; ++rotation) {
            if (!frame.rotations[rotation].has_value()) {
                continue;
            }
            const SpriteRotation& entry = *frame.rotations[rotation];
            if (seen.insert(entry.texturePath).second) {
                uniquePaths.push_back(entry.texturePath);
            }
            if (entry.hitMaskPath.has_value()) {
                textureHitPaths.emplace(entry.texturePath, *entry.hitMaskPath);
            }
        }
    }

    struct PackedImage {
        std::string path;
        Image image{};
        int width = 0;
        int height = 0;
        int packWidth = 0;
        int packHeight = 0;
    };

    std::vector<PackedImage> images;
    images.reserve(uniquePaths.size());
    for (const std::string& path : uniquePaths) {
        const auto resolved = resolveTexturePath(path);
        if (!resolved) {
            TraceLog(LOG_WARNING, "Sprite texture not found: %s", path.c_str());
            continue;
        }

        Image image = LoadImage(resolved->string().c_str());
        if (image.data == nullptr) {
            TraceLog(LOG_WARNING, "Failed to load sprite texture: %s", path.c_str());
            continue;
        }
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        PackedImage packed{};
        packed.path = path;
        packed.image = image;
        packed.width = image.width;
        packed.height = image.height;
        packed.packWidth = image.width + kAtlasPadding * 2;
        packed.packHeight = image.height + kAtlasPadding * 2;
        images.push_back(std::move(packed));
    }

    std::sort(images.begin(), images.end(), [](const PackedImage& a, const PackedImage& b) {
        if (a.packHeight != b.packHeight) {
            return a.packHeight > b.packHeight;
        }
        return a.packWidth > b.packWidth;
    });

    int atlasSize = kDefaultAtlasSize;
    for (const PackedImage& image : images) {
        atlasSize = std::max(atlasSize, nextPowerOfTwo(std::max(image.packWidth, image.packHeight)));
    }

    struct ShelfAtlas {
        Image image{};
        int cursorX = 0;
        int cursorY = 0;
        int rowHeight = 0;
    };

    std::vector<ShelfAtlas> shelves;

    auto ensureShelf = [&](int index) {
        while (static_cast<int>(shelves.size()) <= index) {
            ShelfAtlas shelf{};
            shelf.image = GenImageColor(atlasSize, atlasSize, BLANK);
            ImageFormat(&shelf.image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            shelves.push_back(std::move(shelf));
        }
    };

    auto newShelf = [&](int& atlasIndex, ShelfAtlas*& shelf) {
        ++atlasIndex;
        ensureShelf(atlasIndex);
        shelf = &shelves[static_cast<std::size_t>(atlasIndex)];
        shelf->cursorX = 0;
        shelf->cursorY = 0;
        shelf->rowHeight = 0;
    };

    int atlasIndex = 0;
    ensureShelf(atlasIndex);
    ShelfAtlas* shelf = &shelves[0];

    for (PackedImage& image : images) {
        if (image.packWidth > atlasSize || image.packHeight > atlasSize) {
            UnloadImage(image.image);
            image.image = {};
            continue;
        }

        if (shelf->cursorX + image.packWidth > atlasSize) {
            shelf->cursorX = 0;
            shelf->cursorY += shelf->rowHeight;
            shelf->rowHeight = 0;
        }
        if (shelf->cursorY + image.packHeight > atlasSize) {
            newShelf(atlasIndex, shelf);
        }

        const int destX = shelf->cursorX + kAtlasPadding;
        const int destY = shelf->cursorY + kAtlasPadding;
        ImageDrawImage(
            &shelf->image,
            image.image,
            destX,
            destY,
            WHITE);

        SpriteAtlasRect rect{};
        rect.atlasIndex = atlasIndex;
        rect.source = {
            static_cast<float>(destX),
            static_cast<float>(destY),
            static_cast<float>(image.width),
            static_cast<float>(image.height),
        };
        atlas.rects.emplace(image.path, rect);

        SpriteHitmask hitmask{};
        const auto hitPathIt = textureHitPaths.find(image.path);
        if (hitPathIt != textureHitPaths.end() && !asset.hitParts.empty()) {
            const auto resolvedHit = resolveTexturePath(hitPathIt->second);
            if (resolvedHit) {
                Image hitImage = LoadImage(resolvedHit->string().c_str());
                if (hitImage.data != nullptr) {
                    ImageFormat(&hitImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                    if (hitImage.width == image.width && hitImage.height == image.height) {
                        hitmask = bakeColoredHitmask(hitImage, asset.hitParts);
                    } else {
                        TraceLog(
                            LOG_WARNING,
                            "Sprite hit mask size mismatch for %s (%dx%d vs %dx%d), using alpha",
                            hitPathIt->second.c_str(),
                            hitImage.width,
                            hitImage.height,
                            image.width,
                            image.height);
                        hitmask = bakeSingleHitmask(image.image);
                    }
                    UnloadImage(hitImage);
                } else {
                    TraceLog(LOG_WARNING, "Failed to load sprite hit mask: %s", hitPathIt->second.c_str());
                    hitmask = bakeSingleHitmask(image.image);
                }
            } else {
                TraceLog(LOG_WARNING, "Sprite hit mask not found: %s", hitPathIt->second.c_str());
                hitmask = bakeSingleHitmask(image.image);
            }
        } else {
            hitmask = bakeSingleHitmask(image.image);
        }
        atlas.hitmasks.emplace(image.path, std::move(hitmask));

        shelf->cursorX += image.packWidth;
        shelf->rowHeight = std::max(shelf->rowHeight, image.packHeight);

        UnloadImage(image.image);
        image.image = {};
    }

    atlas.textures.reserve(shelves.size());
    for (ShelfAtlas& shelfImage : shelves) {
        Texture2D texture = LoadTextureFromImage(shelfImage.image);
        if (texture.id != 0) {
            SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
            SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        }
        atlas.textures.push_back(texture);
        UnloadImage(shelfImage.image);
        shelfImage.image = {};
    }

    return atlas;
}

void unloadSpriteAtlas(SpriteAtlas& atlas) {
    for (Texture2D& texture : atlas.textures) {
        if (texture.id != 0) {
            UnloadTexture(texture);
            texture = {};
        }
    }
    atlas.textures.clear();
    atlas.rects.clear();
    atlas.hitmasks.clear();
}

std::uint8_t hitmaskPartAt(const SpriteHitmask& mask, int x, int y) {
    if (x < 0 || y < 0 || x >= mask.width || y >= mask.height || mask.parts.empty()) {
        return 0;
    }
    return mask.parts[static_cast<std::size_t>(y * mask.width + x)];
}

bool hitmaskTest(const SpriteHitmask& mask, int x, int y) {
    return hitmaskPartAt(mask, x, y) != 0;
}

bool hitmaskTestUv(const SpriteHitmask& mask, float u, float v, bool mirror) {
    if (mask.width <= 0 || mask.height <= 0) {
        return false;
    }
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }

    int x = static_cast<int>(std::floor(u * static_cast<float>(mask.width)));
    int y = static_cast<int>(std::floor((1.0f - v) * static_cast<float>(mask.height)));
    if (x >= mask.width) {
        x = mask.width - 1;
    }
    if (y >= mask.height) {
        y = mask.height - 1;
    }
    if (mirror) {
        x = mask.width - 1 - x;
    }
    return hitmaskTest(mask, x, y);
}

const char* hitmaskPartName(const SpriteHitmask& mask, std::uint8_t part) {
    if (part == 0 || part > mask.partNames.size()) {
        return "none";
    }
    return mask.partNames[static_cast<std::size_t>(part - 1)].c_str();
}

const SpriteFrame* findSpriteFrame(const SpriteAsset& asset, std::string_view frameId) {
    for (const SpriteFrame& frame : asset.frames) {
        if (frame.id == frameId) {
            return &frame;
        }
    }
    if (!asset.frames.empty()) {
        return &asset.frames.front();
    }
    return nullptr;
}

const SpriteRotation* selectSpriteRotation(const SpriteFrame& frame, int rotation) {
    if (rotation >= 0 && rotation < kSpriteRotationCount && frame.rotations[rotation].has_value()) {
        return &*frame.rotations[rotation];
    }

    if (frame.rotations[0].has_value()) {
        return &*frame.rotations[0];
    }

    if (rotation >= 1 && rotation <= 8) {
        for (int distance = 1; distance <= 4; ++distance) {
            const int left = ((rotation - 1 - distance + 8) % 8) + 1;
            const int right = ((rotation - 1 + distance) % 8) + 1;
            if (frame.rotations[left].has_value()) {
                return &*frame.rotations[left];
            }
            if (frame.rotations[right].has_value()) {
                return &*frame.rotations[right];
            }
        }
    }

    for (int index = 1; index < kSpriteRotationCount; ++index) {
        if (frame.rotations[index].has_value()) {
            return &*frame.rotations[index];
        }
    }

    return nullptr;
}

int doomRotationFromViewYaw(float relativeYawRadians) {
    float yaw = relativeYawRadians;
    constexpr float kTwoPi = 6.28318530718f;
    while (yaw < 0.0f) {
        yaw += kTwoPi;
    }
    while (yaw >= kTwoPi) {
        yaw -= kTwoPi;
    }

    constexpr float kSector = kTwoPi / 8.0f;
    const int sector = static_cast<int>(std::floor((yaw + kSector * 0.5f) / kSector)) % 8;
    return sector + 1;
}

}
