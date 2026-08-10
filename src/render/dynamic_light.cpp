#include "render/dynamic_light.hpp"

#include "render/components.hpp"

#include <flecs.h>
#include <raymath.h>

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float wrapHue(float hueDegrees) {
    float hue = std::fmod(hueDegrees, 360.0f);
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    return hue;
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

Vector3 hsvToRgb(Vector3 hsv) {
    const float h = wrapHue(hsv.x);
    const float s = saturate(hsv.y);
    const float v = saturate(hsv.z);
    const Color color = ColorFromHSV(h, s, v);
    return {
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
    };
}

Vector3 rgbToHsv(Vector3 rgb) {
    const Color color{
        static_cast<unsigned char>(std::lround(saturate(rgb.x) * 255.0f)),
        static_cast<unsigned char>(std::lround(saturate(rgb.y) * 255.0f)),
        static_cast<unsigned char>(std::lround(saturate(rgb.z) * 255.0f)),
        255,
    };
    return ColorToHSV(color);
}

Vector3 hslToRgb(Vector3 hsl) {
    const float h = wrapHue(hsl.x) / 360.0f;
    const float s = saturate(hsl.y);
    const float l = saturate(hsl.z);
    if (s <= 1e-6f) {
        return {l, l, l};
    }

    const auto hueToRgb = [](float p, float q, float t) {
        float tt = t;
        if (tt < 0.0f) {
            tt += 1.0f;
        }
        if (tt > 1.0f) {
            tt -= 1.0f;
        }
        if (tt < 1.0f / 6.0f) {
            return p + (q - p) * 6.0f * tt;
        }
        if (tt < 1.0f / 2.0f) {
            return q;
        }
        if (tt < 2.0f / 3.0f) {
            return p + (q - p) * (2.0f / 3.0f - tt) * 6.0f;
        }
        return p;
    };

    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    return {
        hueToRgb(p, q, h + 1.0f / 3.0f),
        hueToRgb(p, q, h),
        hueToRgb(p, q, h - 1.0f / 3.0f),
    };
}

Vector3 rgbToHsl(Vector3 rgb) {
    const float r = saturate(rgb.x);
    const float g = saturate(rgb.y);
    const float b = saturate(rgb.z);
    const float maxC = std::max(r, std::max(g, b));
    const float minC = std::min(r, std::min(g, b));
    const float l = (maxC + minC) * 0.5f;
    if (maxC - minC < 1e-6f) {
        return {0.0f, 0.0f, l};
    }

    const float d = maxC - minC;
    const float s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);
    float h = 0.0f;
    if (maxC == r) {
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    } else if (maxC == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h *= 60.0f;
    return {h, s, l};
}

Vector3 colorValueAsRgb(const DynamicLightColor& color) {
    switch (color.space) {
    case DynamicLightColorSpace::Hsv:
        return hsvToRgb(color.value);
    case DynamicLightColorSpace::Hsl:
        return hslToRgb(color.value);
    case DynamicLightColorSpace::Rgb:
    default:
        return {
            saturate(color.value.x),
            saturate(color.value.y),
            saturate(color.value.z),
        };
    }
}

} // namespace

Vector3 dynamicLightLinearRgb(const DynamicLight& light) {
    return colorValueAsRgb(light.color);
}

void setDynamicLightRgb(DynamicLight& light, Vector3 rgb) {
    light.color.space = DynamicLightColorSpace::Rgb;
    light.color.value = {
        saturate(rgb.x),
        saturate(rgb.y),
        saturate(rgb.z),
    };
}

void setDynamicLightHsv(DynamicLight& light, Vector3 hsv) {
    light.color.space = DynamicLightColorSpace::Hsv;
    light.color.value = {
        wrapHue(hsv.x),
        saturate(hsv.y),
        saturate(hsv.z),
    };
}

void setDynamicLightHsl(DynamicLight& light, Vector3 hsl) {
    light.color.space = DynamicLightColorSpace::Hsl;
    light.color.value = {
        wrapHue(hsl.x),
        saturate(hsl.y),
        saturate(hsl.z),
    };
}

void modulateDynamicLightHsv(DynamicLight& light, Vector3 hsvDelta) {
    Vector3 hsv = light.color.space == DynamicLightColorSpace::Hsv
        ? light.color.value
        : rgbToHsv(colorValueAsRgb(light.color));
    hsv.x = wrapHue(hsv.x + hsvDelta.x);
    hsv.y = saturate(hsv.y + hsvDelta.y);
    hsv.z = saturate(hsv.z + hsvDelta.z);
    setDynamicLightHsv(light, hsv);
}

void modulateDynamicLightHsl(DynamicLight& light, Vector3 hslDelta) {
    Vector3 hsl = light.color.space == DynamicLightColorSpace::Hsl
        ? light.color.value
        : rgbToHsl(colorValueAsRgb(light.color));
    hsl.x = wrapHue(hsl.x + hslDelta.x);
    hsl.y = saturate(hsl.y + hslDelta.y);
    hsl.z = saturate(hsl.z + hslDelta.z);
    setDynamicLightHsl(light, hsl);
}

Vector3 dynamicLightDirectionFromRotation(Quaternion rotation) {
    return Vector3Normalize(Vector3RotateByQuaternion({0.0f, 0.0f, 1.0f}, rotation));
}

flecs::entity spawnDynamicLight(
    flecs::world& world,
    const char* name,
    Vector3 position,
    Quaternion rotation,
    const DynamicLight& light) {
    flecs::entity entity = name != nullptr && name[0] != '\0' ? world.entity(name) : world.entity();
    LocalTransformation local{};
    local.position = position;
    local.scale = {1.0f, 1.0f, 1.0f};
    local.rotation = rotation;
    Matrix s = MatrixScale(local.scale.x, local.scale.y, local.scale.z);
    Matrix r = QuaternionToMatrix(local.rotation);
    Matrix t = MatrixTranslate(local.position.x, local.position.y, local.position.z);
    GlobalTransformation global{};
    global.matrix = MatrixMultiply(t, MatrixMultiply(r, s));
    entity.add<WorldSpace>()
        .set<LocalTransformation>(local)
        .set<GlobalTransformation>(global)
        .set<DynamicLight>(light);
    return entity;
}

Vector3 evaluateDynamicLightAtPoint(
    const RankedDynamicLight& light,
    Vector3 point,
    Vector3 normal) {
    Vector3 delta = Vector3Subtract(light.position, point);
    const float dist2Raw = Vector3DotProduct(delta, delta);
    if (dist2Raw < 1e-6f) {
        return {};
    }
    const float dist = std::sqrt(dist2Raw);
    const float range = std::max(light.light.range, 1e-4f);
    if (dist > range) {
        return {};
    }

    const Vector3 toLight = Vector3Scale(delta, 1.0f / dist);
    const float ndotl = Vector3DotProduct(normal, toLight);
    if (ndotl <= 0.0f) {
        return {};
    }

    const float t = dist / range;
    float atten = std::max(0.0f, 1.0f - t * t);
    atten *= atten;
    atten *= ndotl;

    float spot = 1.0f;
    if (light.light.kind == DynamicLightKind::Spot) {
        const Vector3 forward = Vector3Normalize(light.direction);
        const float cosTheta = Vector3DotProduct(Vector3Scale(toLight, -1.0f), forward);
        const float cosOuter = std::cos(light.light.coneAngle);
        const float cosInner = std::cos(light.light.coneAngle * 0.7f);
        spot = smoothstep(cosOuter, cosInner, cosTheta);
        if (spot <= 0.0f) {
            return {};
        }
    }

    const float scale = atten * spot * light.light.intensity;
    return {
        light.linearRgb.x * scale,
        light.linearRgb.y * scale,
        light.linearRgb.z * scale,
    };
}

Vector3 evaluateDynamicLightsAtPoint(
    const std::vector<RankedDynamicLight>& lights,
    Vector3 point,
    Vector3 normal) {
    Vector3 total{};
    for (const RankedDynamicLight& light : lights) {
        const Vector3 contrib = evaluateDynamicLightAtPoint(light, point, normal);
        total.x += contrib.x;
        total.y += contrib.y;
        total.z += contrib.z;
    }
    return total;
}

std::vector<RankedDynamicLight> rankDynamicLights(
    const std::vector<RankedDynamicLight>& candidates,
    Vector3 focus,
    int maxLights,
    int maxShadowed) {
    std::vector<RankedDynamicLight> ranked = candidates;
    for (RankedDynamicLight& light : ranked) {
        const float dist = Vector3Distance(light.position, focus);
        const float range = std::max(light.light.range, 1e-4f);
        const float intensity = std::max(light.light.intensity, 0.0f);
        light.score = intensity * range / (1.0f + dist);
        light.shadowSlot = -1;
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedDynamicLight& a, const RankedDynamicLight& b) {
        return a.score > b.score;
    });

    if (static_cast<int>(ranked.size()) > maxLights) {
        ranked.resize(static_cast<std::size_t>(maxLights));
    }

    int shadowed = 0;
    for (RankedDynamicLight& light : ranked) {
        if (!light.light.castShadows || shadowed >= maxShadowed) {
            light.shadowSlot = -1;
            continue;
        }
        light.shadowSlot = shadowed;
        ++shadowed;
    }
    return ranked;
}

}
