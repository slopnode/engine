#include "assets/material_loader.hpp"

#include "core/sexpr.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

namespace slopengine {

namespace {

Color colorFromNormalized(float r, float g, float b, float a) {
    auto channel = [](float value) {
        return static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return {channel(r), channel(g), channel(b), channel(a)};
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

bool applySoundField(const Sexpr& form, MaterialAsset& asset, SexprParseError& error) {
    if (!form.isList() || !form.list[0].isAtom("sound")) {
        error = {"(sound (absorption lo mid hi) (scattering n) (transmission lo mid hi))", form.line, form.column};
        return false;
    }
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        const Sexpr& clause = form.list[i];
        if (!clause.isList() || clause.list.empty() || clause.list[0].kind != SexprKind::Atom) {
            error = {"sound expected (absorption|scattering|transmission ...)", clause.line, clause.column};
            return false;
        }
        const std::string& clauseTag = clause.list[0].text;
        if (clauseTag == "absorption") {
            float values[3] = {};
            if (!readNumberField(clause, 3, values)) {
                error = {"(absorption lo mid hi)", clause.line, clause.column};
                return false;
            }
            asset.sound.absorptionLow = values[0];
            asset.sound.absorptionMid = values[1];
            asset.sound.absorptionHigh = values[2];
        } else if (clauseTag == "scattering") {
            if (!readNumberField(clause, 1, &asset.sound.scattering)) {
                error = {"(scattering n)", clause.line, clause.column};
                return false;
            }
        } else if (clauseTag == "transmission") {
            float values[3] = {};
            if (!readNumberField(clause, 3, values)) {
                error = {"(transmission lo mid hi)", clause.line, clause.column};
                return false;
            }
            asset.sound.transmissionLow = values[0];
            asset.sound.transmissionMid = values[1];
            asset.sound.transmissionHigh = values[2];
        } else {
            error = {"unknown sound field '" + clauseTag + "'", clause.line, clause.column};
            return false;
        }
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
    if (tag == "texture-graph") {
        if (!readStringField(form, asset.textureGraphPath)) {
            error = {"(texture-graph \"path\")", form.line, form.column};
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
    if (tag == "deform-mask") {
        if (!readStringField(form, asset.deformMask)) {
            error = {"(deform-mask \"path\")", form.line, form.column};
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
    if (tag == "precise-emission") {
        if (form.list.size() == 1) {
            asset.preciseEmission = true;
            return true;
        }
        float flag = 1.0f;
        if (!readNumberField(form, 1, &flag)) {
            error = {"(precise-emission) or (precise-emission 0|1)", form.line, form.column};
            return false;
        }
        asset.preciseEmission = flag != 0.0f;
        return true;
    }
    if (tag == "exact-emission") {
        if (form.list.size() == 1) {
            asset.exactEmission = true;
            return true;
        }
        float flag = 1.0f;
        if (!readNumberField(form, 1, &flag)) {
            error = {"(exact-emission) or (exact-emission 0|1)", form.line, form.column};
            return false;
        }
        asset.exactEmission = flag != 0.0f;
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
    if (tag == "emission-blend") {
        const char* usage = "(emission-blend \"add\"|\"multiply\" [scale])";
        if (!form.isList() || form.list.size() < 2 || form.list.size() > 3 ||
            !form.list[1].isString()) {
            error = {usage, form.line, form.column};
            return false;
        }
        const std::string& mode = form.list[1].text;
        if (mode != "add" && mode != "multiply") {
            error = {usage, form.line, form.column};
            return false;
        }
        asset.emissionMultiply = mode == "multiply";
        if (form.list.size() == 3) {
            if (!form.list[2].isNumber()) {
                error = {usage, form.line, form.column};
                return false;
            }
            asset.emissionBlendScale = static_cast<float>(form.list[2].number);
        } else {
            asset.emissionBlendScale = 1.0f;
        }
        return true;
    }
    if (tag == "bake-emission") {
        if (form.list.size() == 1) {
            asset.bakeEmission = true;
            return true;
        }
        float flag = 1.0f;
        if (!readNumberField(form, 1, &flag)) {
            error = {"(bake-emission) or (bake-emission 0|1)", form.line, form.column};
            return false;
        }
        asset.bakeEmission = flag != 0.0f;
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
    if (tag == "sound") {
        return applySoundField(form, asset, error);
    }
    if (tag == "sky-cylinder") {
        if (!readStringField(form, asset.skyCylinderTexture)) {
            error = {"(sky-cylinder \"path\")", form.line, form.column};
            return false;
        }
        asset.skyMode = SkyboxMode::Cylinder;
        asset.haveSkyMode = true;
        return true;
    }
    if (tag == "sky-cylinder-offset") {
        if (!readNumberField(form, 1, &asset.skyCylinderOffset)) {
            error = {"(sky-cylinder-offset n)", form.line, form.column};
            return false;
        }
        return true;
    }
    if (tag == "sky-cylinder-scale") {
        if (!readNumberField(form, 1, &asset.skyCylinderScale)) {
            error = {"(sky-cylinder-scale n)", form.line, form.column};
            return false;
        }
        return true;
    }
    if (tag == "sky-cylinder-repeat") {
        float repeat = 1.0f;
        if (!readNumberField(form, 1, &repeat)) {
            error = {"(sky-cylinder-repeat n)", form.line, form.column};
            return false;
        }
        asset.skyCylinderRepeat = std::max(1, static_cast<int>(repeat));
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
    int skyAppearanceCount = 0;
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        const Sexpr& field = root.list[i];
        if (field.isList() && !field.list.empty() && field.list[0].kind == SexprKind::Atom) {
            const std::string& tag = field.list[0].text;
            if (tag == "sky-color" || tag == "sky-gradient" || tag == "sky-cube" || tag == "sky-cylinder") {
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
    const int albedoSources = (asset.albedoTexture.empty() ? 0 : 1) + (asset.textureAnimPath.empty() ? 0 : 1) +
        (asset.textureGraphPath.empty() ? 0 : 1);
    if (albedoSources > 1) {
        return false;
    }
    return true;
}

Material createRaylibMaterial(
    const MaterialAsset& asset,
    const TextureResolver& resolveTexture,
    const TextureAnimFrameResolver& resolveAnimFrame,
    const TextureGraphOutputResolver& resolveGraphOutput) {
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
    } else if (!asset.textureGraphPath.empty() && resolveGraphOutput) {
        const Texture2D texture = resolveGraphOutput(asset.textureGraphPath, "albedo");
        if (texture.id != 0) {
            SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
            SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, texture);
        }
    }

    if (!asset.deformMask.empty() && resolveTexture) {
        const Texture2D mask = resolveTexture(asset.deformMask);
        if (mask.id != 0) {
            SetMaterialTexture(&material, MATERIAL_MAP_HEIGHT, mask);
        }
    }

    if (asset.emissionPower > 0.0f) {
        material.maps[MATERIAL_MAP_SPECULAR].color = asset.emissionColor;
    } else {
        material.maps[MATERIAL_MAP_SPECULAR].color = {0, 0, 0, 255};
    }
    // colSpecular.a is otherwise unused by the world shader; repurposed here as the render-time
    // fullbright toggle *and* blend mode (see lightmap_frag.glsl's fullbright mix): 0 = off,
    // ~0.5 (128) = on/multiply, 1.0 (255) = on/add. Packed into one byte instead of a dedicated
    // uniform since raylib already auto-uploads colSpecular for every DrawMesh call for free.
    if (!asset.fullbright) {
        material.maps[MATERIAL_MAP_SPECULAR].color.a = 0;
    } else {
        material.maps[MATERIAL_MAP_SPECULAR].color.a = asset.emissionMultiply ? 128 : 255;
    }
    // colSpecular.rgb is normalized 0-1 by raylib (8-bit Color channels), so it can't carry
    // emissionPower's HDR magnitude (e.g. 6.0). Stash a value in the unused .value float instead -
    // the render pass uploads it to a dedicated "emissionPower" uniform per draw call (see
    // lightmap_frag.glsl) so masked emissive texels can blow out past 1.0 through tonemap.
    // Dual-purpose with the blend mode above, since a material is only ever one mode: in "add"
    // mode this is the emission magnitude (emissionPower*emissionBlendScale - scaled here, not at
    // bake time, so emissionBlendScale only affects this material's own render and not how much
    // light it casts on neighbors via radiosity). In "multiply" mode emissionPower itself isn't
    // used at render time at all, so this instead carries emissionBlendScale directly, read by the
    // shader as the brightness target for the masked texels.
    material.maps[MATERIAL_MAP_SPECULAR].value =
        asset.emissionMultiply ? asset.emissionBlendScale : asset.emissionPower * asset.emissionBlendScale;

    Texture2D graphEmission{};
    if (asset.emissionTexture.empty() && !asset.textureGraphPath.empty() && resolveGraphOutput) {
        graphEmission = resolveGraphOutput(asset.textureGraphPath, "emission");
    }
    if (graphEmission.id != 0) {
        SetTextureWrap(graphEmission, TEXTURE_WRAP_REPEAT);
        SetMaterialTexture(&material, MATERIAL_MAP_EMISSION, graphEmission);
    } else if (!asset.emissionTexture.empty() && resolveTexture) {
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

namespace {

std::string fmtMaterialNumber(float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    return buf;
}

std::string fmtMaterialColor(Color color) {
    return fmtMaterialNumber(color.r / 255.0f) + " " + fmtMaterialNumber(color.g / 255.0f) + " " +
        fmtMaterialNumber(color.b / 255.0f) + " " + fmtMaterialNumber(color.a / 255.0f);
}

std::string fmtMaterialVec3(Vector3 v) {
    return fmtMaterialNumber(v.x) + " " + fmtMaterialNumber(v.y) + " " + fmtMaterialNumber(v.z);
}

std::string quoteMaterialString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // namespace

std::string serializeMaterialAsset(const MaterialAsset& asset) {
    const MaterialAsset defaults{};
    std::ostringstream out;
    out << "(material\n";
    out << "  (shader " << quoteMaterialString(asset.shader) << ")\n";
    if (!asset.albedoTexture.empty()) {
        out << "  (texture " << quoteMaterialString(asset.albedoTexture) << ")\n";
    } else if (!asset.textureAnimPath.empty()) {
        out << "  (texture-anim " << quoteMaterialString(asset.textureAnimPath) << ")\n";
    } else if (!asset.textureGraphPath.empty()) {
        out << "  (texture-graph " << quoteMaterialString(asset.textureGraphPath) << ")\n";
    }
    out << "  (texel-size " << fmtMaterialNumber(asset.pixelsPerMeter) << ")\n";
    out << "  (base-color " << fmtMaterialColor(asset.baseColor) << ")\n";
    if (asset.ior != defaults.ior) {
        out << "  (ior " << fmtMaterialNumber(asset.ior) << ")\n";
    }
    if (!asset.emissionTexture.empty()) {
        out << "  (emission " << quoteMaterialString(asset.emissionTexture) << ")\n";
    }
    if (!asset.deformMask.empty()) {
        out << "  (deform-mask " << quoteMaterialString(asset.deformMask) << ")\n";
    }
    if (asset.emissionColor.r != defaults.emissionColor.r || asset.emissionColor.g != defaults.emissionColor.g ||
        asset.emissionColor.b != defaults.emissionColor.b || asset.emissionColor.a != defaults.emissionColor.a) {
        out << "  (emission-color " << fmtMaterialColor(asset.emissionColor) << ")\n";
    }
    if (asset.emissionPower != defaults.emissionPower) {
        out << "  (emission-power " << fmtMaterialNumber(asset.emissionPower) << ")\n";
    }
    if (asset.emissionRange != defaults.emissionRange) {
        out << "  (emission-range " << fmtMaterialNumber(asset.emissionRange) << ")\n";
    }
    if (asset.preciseEmission) {
        out << "  (precise-emission)\n";
    }
    if (asset.exactEmission) {
        out << "  (exact-emission)\n";
    }
    if (asset.fullbright) {
        out << "  (fullbright)\n";
    }
    if (asset.emissionMultiply || asset.emissionBlendScale != defaults.emissionBlendScale) {
        out << "  (emission-blend " << (asset.emissionMultiply ? "\"multiply\"" : "\"add\"") << " "
            << fmtMaterialNumber(asset.emissionBlendScale) << ")\n";
    }
    if (asset.bakeEmission) {
        out << "  (bake-emission)\n";
    }
    if (asset.sky) {
        out << "  (sky)\n";
    }
    if (asset.haveSkyMode) {
        switch (asset.skyMode) {
        case SkyboxMode::Solid:
            out << "  (sky-color " << fmtMaterialVec3(asset.skySolidColor) << ")\n";
            break;
        case SkyboxMode::Gradient:
            out << "  (sky-gradient\n";
            for (int i = 0; i < asset.skyGradientStopCount; ++i) {
                const SkyGradientStop& stop = asset.skyGradientStops[static_cast<std::size_t>(i)];
                out << "    (stop " << fmtMaterialNumber(stop.position) << " " << fmtMaterialVec3(stop.color) << ")\n";
            }
            out << "  )\n";
            break;
        case SkyboxMode::Cube: {
            static const char* kFaceTags[6] = {"px", "nx", "py", "ny", "pz", "nz"};
            out << "  (sky-cube";
            for (int i = 0; i < 6; ++i) {
                out << " (" << kFaceTags[i] << " " << quoteMaterialString(asset.skyCubeFaces[i]) << ")";
            }
            out << ")\n";
            break;
        }
        case SkyboxMode::Cylinder:
            out << "  (sky-cylinder " << quoteMaterialString(asset.skyCylinderTexture) << ")\n";
            if (asset.skyCylinderOffset != defaults.skyCylinderOffset) {
                out << "  (sky-cylinder-offset " << fmtMaterialNumber(asset.skyCylinderOffset) << ")\n";
            }
            if (asset.skyCylinderScale != defaults.skyCylinderScale) {
                out << "  (sky-cylinder-scale " << fmtMaterialNumber(asset.skyCylinderScale) << ")\n";
            }
            if (asset.skyCylinderRepeat != defaults.skyCylinderRepeat) {
                out << "  (sky-cylinder-repeat " << asset.skyCylinderRepeat << ")\n";
            }
            break;
        }
    }
    const MaterialSoundProfile& s = asset.sound;
    const MaterialSoundProfile& d = defaults.sound;
    const bool absorptionChanged = s.absorptionLow != d.absorptionLow || s.absorptionMid != d.absorptionMid ||
        s.absorptionHigh != d.absorptionHigh;
    const bool scatteringChanged = s.scattering != d.scattering;
    const bool transmissionChanged = s.transmissionLow != d.transmissionLow ||
        s.transmissionMid != d.transmissionMid || s.transmissionHigh != d.transmissionHigh;
    if (absorptionChanged || scatteringChanged || transmissionChanged) {
        out << "  (sound";
        if (absorptionChanged) {
            out << " (absorption " << fmtMaterialNumber(s.absorptionLow) << " " << fmtMaterialNumber(s.absorptionMid)
                << " " << fmtMaterialNumber(s.absorptionHigh) << ")";
        }
        if (scatteringChanged) {
            out << " (scattering " << fmtMaterialNumber(s.scattering) << ")";
        }
        if (transmissionChanged) {
            out << " (transmission " << fmtMaterialNumber(s.transmissionLow) << " "
                << fmtMaterialNumber(s.transmissionMid) << " " << fmtMaterialNumber(s.transmissionHigh) << ")";
        }
        out << ")\n";
    }
    out << ")\n";
    return out.str();
}

}
