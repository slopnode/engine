#include "assets/material_loader.hpp"

#include <charconv>
#include <optional>
#include <string>

namespace slopengine {

namespace {

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

Color colorFromNormalized(float r, float g, float b, float a) {
    return {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        static_cast<unsigned char>(a * 255.0f),
    };
}

} // namespace

bool parseMaterialAsset(std::string_view source, MaterialAsset& asset) {
    asset = {};

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (auto shader = readQuotedField(line, "(shader ")) {
            asset.shader = *shader;
        } else if (auto texture = readQuotedField(line, "(texture ")) {
            asset.albedoTexture = *texture;
        } else if (line.rfind("(base-color ", 0) == 0) {
            float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (!readFloats(line.substr(std::string_view("(base-color ").size()), 4, rgba)) {
                return false;
            }
            asset.baseColor = colorFromNormalized(rgba[0], rgba[1], rgba[2], rgba[3]);
        } else if (line.rfind("(texel-size ", 0) == 0) {
            float texelSize = 64.0f;
            if (!readFloats(line.substr(std::string_view("(texel-size ").size()), 1, &texelSize)) {
                return false;
            }
            asset.pixelsPerMeter = texelSize;
        } else if (auto emission = readQuotedField(line, "(emission ")) {
            asset.emissionTexture = *emission;
        } else if (line.rfind("(emission-color ", 0) == 0) {
            float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (!readFloats(line.substr(std::string_view("(emission-color ").size()), 4, rgba)) {
                return false;
            }
            asset.emissionColor = colorFromNormalized(rgba[0], rgba[1], rgba[2], rgba[3]);
        } else if (line.rfind("(emission-power ", 0) == 0) {
            float power = 0.0f;
            if (!readFloats(line.substr(std::string_view("(emission-power ").size()), 1, &power)) {
                return false;
            }
            asset.emissionPower = power;
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return true;
}

Material createRaylibMaterial(const MaterialAsset& asset, const TextureResolver& resolveTexture) {
    Material material = LoadMaterialDefault();
    material.maps[MATERIAL_MAP_ALBEDO].color = asset.baseColor;
    if (!asset.albedoTexture.empty() && resolveTexture) {
        const Texture2D texture = resolveTexture(asset.albedoTexture);
        if (texture.id != 0) {
            SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
            SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, texture);
        }
    }

    if (asset.emissionPower > 0.0f) {
        material.maps[MATERIAL_MAP_SPECULAR].color = asset.emissionColor;
        material.maps[MATERIAL_MAP_SPECULAR].color.a = 255;
    } else {
        material.maps[MATERIAL_MAP_SPECULAR].color = {0, 0, 0, 255};
    }
    return material;
}

}
