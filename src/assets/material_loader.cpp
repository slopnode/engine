#include "assets/material_loader.hpp"

#include "core/sexpr.hpp"

#include <rlgl.h>

#include <algorithm>
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

bool readVec3Field(const Sexpr& form, Vector3& out) {
    float rgb[3] = {};
    if (!readNumberField(form, 3, rgb)) {
        return false;
    }
    out = {rgb[0], rgb[1], rgb[2]};
    return true;
}

bool applySkyCubeFace(const Sexpr& faceClause, MaterialAsset& asset, SexprParseError& error) {
    if (!faceClause.isList() || faceClause.list.empty() ||
        faceClause.list[0].kind != SexprKind::Atom) {
        error = {"sky-cube face expected (tag \"path\")", faceClause.line, faceClause.column};
        return false;
    }
    if (faceClause.list.size() != 2 || !faceClause.list[1].isString()) {
        error = {"sky-cube face expected (tag \"path\")", faceClause.line, faceClause.column};
        return false;
    }
    const std::string& faceTag = faceClause.list[0].text;
    const std::string& path = faceClause.list[1].text;
    if (faceTag == "px") {
        asset.skyCubeFaces[0] = path;
    } else if (faceTag == "nx") {
        asset.skyCubeFaces[1] = path;
    } else if (faceTag == "py") {
        asset.skyCubeFaces[2] = path;
    } else if (faceTag == "ny") {
        asset.skyCubeFaces[3] = path;
    } else if (faceTag == "pz") {
        asset.skyCubeFaces[4] = path;
    } else if (faceTag == "nz") {
        asset.skyCubeFaces[5] = path;
    } else {
        error = {"unknown sky-cube face '" + faceTag + "'", faceClause.line, faceClause.column};
        return false;
    }
    return true;
}

bool applySkyGradientField(const Sexpr& form, MaterialAsset& asset, SexprParseError& error) {
    if (!form.isList() || !form.list[0].isAtom("sky-gradient")) {
        error = {"(sky-gradient (stop ...))", form.line, form.column};
        return false;
    }
    asset.skyMode = SkyboxMode::Gradient;
    asset.haveSkyMode = true;
    asset.skyGradientStopCount = 0;
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        const Sexpr& stopClause = form.list[i];
        if (!stopClause.isList() || !stopClause.list[0].isAtom("stop")) {
            error = {"sky-gradient expected (stop position r g b)", stopClause.line, stopClause.column};
            return false;
        }
        if (stopClause.list.size() != 5) {
            error = {"(stop position r g b)", stopClause.line, stopClause.column};
            return false;
        }
        if (!stopClause.list[1].isNumber() || !stopClause.list[2].isNumber() ||
            !stopClause.list[3].isNumber() || !stopClause.list[4].isNumber()) {
            error = {"(stop position r g b)", stopClause.line, stopClause.column};
            return false;
        }
        if (asset.skyGradientStopCount >= 4) {
            error = {"sky-gradient accepts exactly 4 stops", stopClause.line, stopClause.column};
            return false;
        }
        SkyGradientStop stop{};
        stop.position = static_cast<float>(stopClause.list[1].number);
        stop.color = {
            static_cast<float>(stopClause.list[2].number),
            static_cast<float>(stopClause.list[3].number),
            static_cast<float>(stopClause.list[4].number),
        };
        asset.skyGradientStops[static_cast<std::size_t>(asset.skyGradientStopCount)] = stop;
        ++asset.skyGradientStopCount;
    }
    if (asset.skyGradientStopCount != 4) {
        error = {"sky-gradient requires exactly 4 stops", form.line, form.column};
        return false;
    }
    std::sort(
        asset.skyGradientStops.begin(),
        asset.skyGradientStops.begin() + asset.skyGradientStopCount,
        [](const SkyGradientStop& a, const SkyGradientStop& b) { return a.position < b.position; });
    return true;
}

bool applySkyCubeField(const Sexpr& form, MaterialAsset& asset, SexprParseError& error) {
    if (!form.isList() || !form.list[0].isAtom("sky-cube")) {
        error = {"(sky-cube (px ...) ... (nz ...))", form.line, form.column};
        return false;
    }
    asset.skyMode = SkyboxMode::Cube;
    asset.haveSkyMode = true;
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (!applySkyCubeFace(form.list[i], asset, error)) {
            return false;
        }
    }
    if (asset.skyCubeFaces[0].empty() || asset.skyCubeFaces[1].empty() ||
        asset.skyCubeFaces[2].empty() || asset.skyCubeFaces[3].empty() ||
        asset.skyCubeFaces[4].empty() || asset.skyCubeFaces[5].empty()) {
        error = {"sky-cube requires px nx py ny pz nz faces", form.line, form.column};
        return false;
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
    if (tag == "texture-anim") {
        if (!readStringField(form, asset.textureAnimPath)) {
            error = {"(texture-anim \"path\")", form.line, form.column};
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
    if (tag == "ior") {
        float ior = 1.0f;
        if (!readNumberField(form, 1, &ior)) {
            error = {"(ior n)", form.line, form.column};
            return false;
        }
        asset.ior = ior;
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
    if (tag == "emission-range") {
        float range = 0.0f;
        if (!readNumberField(form, 1, &range)) {
            error = {"(emission-range n)", form.line, form.column};
            return false;
        }
        asset.emissionRange = range;
        return true;
    }
    if (tag == "fullbright") {
        if (form.list.size() == 1) {
            asset.fullbright = true;
            return true;
        }
        float flag = 1.0f;
        if (!readNumberField(form, 1, &flag)) {
            error = {"(fullbright) or (fullbright 0|1)", form.line, form.column};
            return false;
        }
        asset.fullbright = flag != 0.0f;
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
    if (tag == "sky-color") {
        if (!readVec3Field(form, asset.skySolidColor)) {
            error = {"(sky-color r g b)", form.line, form.column};
            return false;
        }
        asset.skyMode = SkyboxMode::Solid;
        asset.haveSkyMode = true;
        return true;
    }
    if (tag == "sky-gradient") {
        return applySkyGradientField(form, asset, error);
    }
    if (tag == "sky-cube") {
        return applySkyCubeField(form, asset, error);
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
    int skyAppearanceCount = 0;
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        const Sexpr& field = root.list[i];
        if (field.isList() && !field.list.empty() && field.list[0].kind == SexprKind::Atom) {
            const std::string& tag = field.list[0].text;
            if (tag == "sky-color" || tag == "sky-gradient" || tag == "sky-cube") {
                ++skyAppearanceCount;
            }
        }
        if (!applyMaterialField(root.list[i], asset, error)) {
            return false;
        }
    }
    if (skyAppearanceCount > 1) {
        return false;
    }
    if (!asset.albedoTexture.empty() && !asset.textureAnimPath.empty()) {
        return false;
    }
    return true;
}

Material createRaylibMaterial(
    const MaterialAsset& asset,
    const TextureResolver& resolveTexture,
    const TextureAnimFrameResolver& resolveAnimFrame) {
    Material material = LoadMaterialDefault();
    material.maps[MATERIAL_MAP_ALBEDO].color = asset.baseColor;
    if (!asset.textureAnimPath.empty() && resolveAnimFrame) {
        const Texture2D texture = resolveAnimFrame(asset.textureAnimPath, 0);
        if (texture.id != 0) {
            SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
            SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, texture);
        }
    } else if (!asset.albedoTexture.empty() && resolveTexture) {
        const Texture2D texture = resolveTexture(asset.albedoTexture);
        if (texture.id != 0) {
            SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
            SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, texture);
        }
    }

    if (asset.emissionPower > 0.0f) {
        material.maps[MATERIAL_MAP_SPECULAR].color = asset.emissionColor;
    } else {
        material.maps[MATERIAL_MAP_SPECULAR].color = {0, 0, 0, 255};
    }
    // colSpecular.a is otherwise unused by the world shader; repurposed here as the
    // render-time "fullbright mask" toggle (see lightmap_frag.glsl's applyFullbrightMask).
    material.maps[MATERIAL_MAP_SPECULAR].color.a = asset.fullbright ? 255 : 0;

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
