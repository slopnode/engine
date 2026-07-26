#include "assets/material_loader.hpp"

#include "core/sexpr.hpp"

#include <rlgl.h>

#include <string>

namespace slopengine {

namespace {

Color colorFromNormalized(float r, float g, float b, float a) {
    return {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        static_cast<unsigned char>(a * 255.0f),
    };
}

bool readStringField(const Sexpr& form, std::string& out) {
    if (!form.isList() || form.list.size() != 2 || !form.list[1].isString()) {
        return false;
    }
    out = form.list[1].text;
    return true;
}

bool readNumberField(const Sexpr& form, std::size_t count, float* out) {
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

bool applyMaterialField(const Sexpr& form, MaterialAsset& asset, SexprParseError& error) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        error = {"expected material field list", form.line, form.column};
        return false;
    }

    const std::string& tag = form.list[0].text;
    if (tag == "shader") {
        if (!readStringField(form, asset.shader)) {
            error = {"(shader \"name\")", form.line, form.column};
            return false;
        }
        return true;
    }
    if (tag == "texture") {
        if (!readStringField(form, asset.albedoTexture)) {
            error = {"(texture \"path\")", form.line, form.column};
            return false;
        }
        return true;
    }
    if (tag == "emission") {
        if (!readStringField(form, asset.emissionTexture)) {
            error = {"(emission \"path\")", form.line, form.column};
            return false;
        }
        return true;
    }
    if (tag == "base-color") {
        float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!readNumberField(form, 4, rgba)) {
            error = {"(base-color r g b a)", form.line, form.column};
            return false;
        }
        asset.baseColor = colorFromNormalized(rgba[0], rgba[1], rgba[2], rgba[3]);
        return true;
    }
    if (tag == "texel-size") {
        float texelSize = 64.0f;
        if (!readNumberField(form, 1, &texelSize)) {
            error = {"(texel-size n)", form.line, form.column};
            return false;
        }
        asset.pixelsPerMeter = texelSize;
        return true;
    }
    if (tag == "emission-color") {
        float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (!readNumberField(form, 4, rgba)) {
            error = {"(emission-color r g b a)", form.line, form.column};
            return false;
        }
        asset.emissionColor = colorFromNormalized(rgba[0], rgba[1], rgba[2], rgba[3]);
        return true;
    }
    if (tag == "emission-power") {
        float power = 0.0f;
        if (!readNumberField(form, 1, &power)) {
            error = {"(emission-power n)", form.line, form.column};
            return false;
        }
        asset.emissionPower = power;
        return true;
    }
    if (tag == "sky") {
        if (form.list.size() == 1) {
            asset.sky = true;
            return true;
        }
        float flag = 1.0f;
        if (!readNumberField(form, 1, &flag)) {
            error = {"(sky) or (sky 0|1)", form.line, form.column};
            return false;
        }
        asset.sky = flag != 0.0f;
        return true;
    }

    error = {"unknown material field '" + tag + "'", form.line, form.column};
    return false;
}

} // namespace

bool parseMaterialAsset(std::string_view source, MaterialAsset& asset) {
    asset = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("material")) {
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    SexprParseError error{};
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        if (!applyMaterialField(root.list[i], asset, error)) {
            return false;
        }
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

    if (!asset.emissionTexture.empty() && resolveTexture) {
        const Texture2D emission = resolveTexture(asset.emissionTexture);
        if (emission.id != 0) {
            SetTextureWrap(emission, TEXTURE_WRAP_REPEAT);
            SetMaterialTexture(&material, MATERIAL_MAP_EMISSION, emission);
        } else {
            material.maps[MATERIAL_MAP_EMISSION].texture = {
                rlGetTextureIdDefault(),
                1,
                1,
                1,
                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
            };
        }
    } else {
        material.maps[MATERIAL_MAP_EMISSION].texture = {
            rlGetTextureIdDefault(),
            1,
            1,
            1,
            PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        };
    }
    return material;
}

}
