#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture5;
uniform vec4 colDiffuse;
uniform vec4 colSpecular;
uniform int useLightmap;
uniform int solidLit;
uniform int lightmapEncoding;
uniform float emissionPower;

const int MAX_DYN_LIGHTS = 128;
const int MAX_SHADOW_SLOTS = 16;
const int SHADOW_FACES = 6;
const int MAX_SHADOW_MAPS = MAX_SHADOW_SLOTS * SHADOW_FACES;
const float SHADOW_MAP_TEXEL = 1.0 / 512.0;

uniform int dynLightCount;
uniform vec4 dynLightPosRange[MAX_DYN_LIGHTS];
uniform vec4 dynLightColorIntensity[MAX_DYN_LIGHTS];
uniform vec4 dynLightDirCone[MAX_DYN_LIGHTS];
uniform vec4 dynLightMeta[MAX_DYN_LIGHTS];

uniform float dynShadowBias;
uniform mat4 dynShadowVp[MAX_SHADOW_MAPS];
uniform sampler2DArray dynShadowMaps;

out vec4 finalColor;

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

float smoothstep2(float edge0, float edge1, float x)
{
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

int cubeFaceIndexAxis(vec3 dir, int axis)
{
    if (axis == 0) {
        return dir.x > 0.0 ? 0 : 1;
    }
    if (axis == 1) {
        return dir.y > 0.0 ? 2 : 3;
    }
    return dir.z > 0.0 ? 4 : 5;
}

float shadowCompareDepth(int mapIndex, vec2 uv, float current)
{
    float closest = texture(dynShadowMaps, vec3(uv, float(mapIndex))).r;
    return current - dynShadowBias > closest ? 0.0 : 1.0;
}

bool shadowSampleFace(int slot, int face, vec3 samplePos, out float visibility)
{
    visibility = 1.0;
    int mapIndex = slot * SHADOW_FACES + face;
    if (mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS) {
        return false;
    }

    vec4 clip = dynShadowVp[mapIndex] * vec4(samplePos, 1.0);
    if (abs(clip.w) < 1e-6) {
        return false;
    }
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z < -1.0 || ndc.z > 1.0 || ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0) {
        return false;
    }
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float current = ndc.z * 0.5 + 0.5;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += shadowCompareDepth(
                mapIndex,
                uv + vec2(float(x), float(y)) * SHADOW_MAP_TEXEL,
                current);
        }
    }
    visibility = sum / 9.0;
    return true;
}

float shadowVisibilitySpot(int slot, vec3 samplePos)
{
    float visibility = 1.0;
    if (shadowSampleFace(slot, 0, samplePos, visibility)) {
        return visibility;
    }
    return 1.0;
}

float shadowVisibilityPoint(int slot, vec3 lightPos, vec3 samplePos)
{
    vec3 dir = samplePos - lightPos;
    vec3 a = abs(dir);
    int primary = 0;
    if (a.y >= a.x && a.y >= a.z) {
        primary = 1;
    } else if (a.z >= a.x && a.z >= a.y) {
        primary = 2;
    }

    float visibility = 1.0;
    if (shadowSampleFace(slot, cubeFaceIndexAxis(dir, primary), samplePos, visibility)) {
        return visibility;
    }

    int secondary = 0;
    if (primary == 0) {
        secondary = (a.y >= a.z) ? 1 : 2;
    } else if (primary == 1) {
        secondary = (a.x >= a.z) ? 0 : 2;
    } else {
        secondary = (a.x >= a.y) ? 0 : 1;
    }
    if (shadowSampleFace(slot, cubeFaceIndexAxis(dir, secondary), samplePos, visibility)) {
        return visibility;
    }
    return 1.0;
}

vec3 evalOneLight(int i, vec3 worldPos, vec3 normal)
{
    vec3 lightPos = dynLightPosRange[i].xyz;
    float range = max(dynLightPosRange[i].w, 1e-4);
    vec3 delta = lightPos - worldPos;
    float dist2Raw = dot(delta, delta);
    if (dist2Raw < 1e-8) {
        return vec3(0.0);
    }
    float dist = sqrt(dist2Raw);
    if (dist > range) {
        return vec3(0.0);
    }
    vec3 toLight = delta / dist;
    float ndotl = dot(normal, toLight);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }
    float t = dist / range;
    float atten = max(0.0, 1.0 - t * t);
    atten *= atten;
    atten *= ndotl;

    float kind = dynLightMeta[i].x;
    float spot = 1.0;
    if (kind > 0.5) {
        vec3 forward = dynLightDirCone[i].xyz;
        float forwardLen = length(forward);
        if (forwardLen < 1e-6) {
            return vec3(0.0);
        }
        forward /= forwardLen;
        float cone = max(dynLightDirCone[i].w, 1e-4);
        float cosTheta = dot(-toLight, forward);
        float cosOuter = cos(cone);
        float cosInner = cos(cone * 0.7);
        spot = smoothstep2(cosOuter, cosInner, cosTheta);
        if (spot <= 0.0) {
            return vec3(0.0);
        }
    }

    float visibility = 1.0;
    int slot = int(dynLightMeta[i].y + 0.5);
    if (slot >= 0 && slot < MAX_SHADOW_SLOTS) {
        vec3 samplePos = worldPos + normal * 0.02;
        if (kind > 0.5) {
            visibility = shadowVisibilitySpot(slot, samplePos);
        } else {
            visibility = shadowVisibilityPoint(slot, lightPos, samplePos);
        }
    }

    vec3 radiance = dynLightColorIntensity[i].rgb * dynLightColorIntensity[i].a;
    return radiance * (atten * spot * visibility);
}

vec3 evalDynamicLights(vec3 worldPos, vec3 normal)
{
    vec3 total = vec3(0.0);
    int count = clamp(dynLightCount, 0, MAX_DYN_LIGHTS);
    for (int i = 0; i < count; ++i) {
        total += evalOneLight(i, worldPos, normal);
    }
    return total;
}

vec3 decodeRgbe(vec4 rgbe)
{
    if (rgbe.a <= 0.0) {
        return vec3(0.0);
    }
    return rgbe.rgb * exp2(rgbe.a * 255.0 - 136.0) * 255.0;
}

vec3 sampleBakedIrradiance(vec2 uv)
{
    if (lightmapEncoding != 0) {
        vec2 texSize = vec2(textureSize(texture1, 0));
        vec2 px = uv * texSize - 0.5;
        vec2 base = floor(px);
        vec2 f = px - base;
        vec2 uv00 = (base + vec2(0.5, 0.5)) / texSize;
        vec2 uv10 = (base + vec2(1.5, 0.5)) / texSize;
        vec2 uv01 = (base + vec2(0.5, 1.5)) / texSize;
        vec2 uv11 = (base + vec2(1.5, 1.5)) / texSize;
        vec3 c00 = decodeRgbe(texture(texture1, uv00));
        vec3 c10 = decodeRgbe(texture(texture1, uv10));
        vec3 c01 = decodeRgbe(texture(texture1, uv01));
        vec3 c11 = decodeRgbe(texture(texture1, uv11));
        vec3 c0 = mix(c00, c10, f.x);
        vec3 c1 = mix(c01, c11, f.x);
        return mix(c0, c1, f.y);
    }
    return texture(texture1, uv).rgb;
}

vec3 tonemapDisplay(vec3 linear)
{
    return linear / (1.0 + max(linear, vec3(0.0)));
}

void main()
{
    vec4 tex = solidLit != 0 ? vec4(1.0) : texture(texture0, fragTexCoord) * colDiffuse;
    vec3 albedoRgb = tex.rgb;
    float albedoA = tex.a;
    vec3 dynamic = evalDynamicLights(fragPosition, normalize(fragNormal));
    vec3 emission = vec3(0.0);
    float emitMask = 0.0;
    if (solidLit == 0 && useLightmap != 0) {
        // Keep the mask texel's own color (emitMap), not just its luma - matching the bake-time
        // formula (emissionAt() in radiosity.cpp: emissionColor*emissionPower * texel). Reducing
        // to a grayscale mask and re-tinting with colSpecular here would throw away the mask
        // texture's actual hue (e.g. orange) and wash everything toward colSpecular's own tint,
        // which is usually a near-white warm grade, not the intended color.
        vec3 emitMap = texture(texture5, fragTexCoord).rgb;
        emitMask = dot(emitMap, vec3(0.2126, 0.7152, 0.0722));
        emission = colSpecular.rgb * emissionPower * emitMap;
    }

    vec3 lightTerm;
    if (useLightmap != 0 && lightmapEncoding != 0) {
        lightTerm = sampleBakedIrradiance(fragTexCoord2) + dynamic;
    } else if (useLightmap != 0) {
        lightTerm = texture(texture1, fragTexCoord2).rgb + dynamic;
    } else {
        lightTerm = fragColor.rgb + dynamic;
    }

    // Doom-style "fullbright": wherever the material's emission mask is lit, guarantee at least
    // fully-lit shading regardless of baked/dynamic light, instead of replacing the whole pixel.
    // Opt-in via colSpecular.a, packed by material_loader.cpp from MaterialAsset::fullbright +
    // emissionMultiply: 0 = off, ~0.5 (128/255) = on/multiply, 1.0 (255) = on/add. Default off, so
    // materials that only use emission for bake-time light emission are unaffected.
    //
    // "add" also layers the HDR `emission` term (colSpecular*emissionPower*emitMap) on top, which
    // goes through the same tonemap curve as everything else below, so a strongly-emissive masked
    // texel still blows out toward white instead of reading as flat, un-boosted albedo - good for
    // a small lamp/strip mask that should read as a light source.
    //
    // "multiply" skips that additive spike and only forces fully-lit shading, so a mask that
    // covers a large area with its own internal detail (e.g. a whole monitor screen) keeps its
    // own texture contrast instead of being washed toward a flat, uniformly saturated color.
    // emissionPower is dual-purpose here (see material_loader.cpp): in add mode it's the emission
    // magnitude used in `emission` above; in multiply mode it instead carries emissionBlendScale,
    // the brightness target below (1.0 = neutral fully-lit; >1.0 pushes brighter while keeping
    // albedo's own color ratios, since it scales lightTerm rather than injecting emission color).
    bool fullbright = colSpecular.a > 0.1;
    bool fullbrightAdditive = colSpecular.a > 0.75;
    if (fullbright) {
        float target = fullbrightAdditive ? 1.0 : emissionPower;
        lightTerm = mix(lightTerm, vec3(target), clamp(emitMask, 0.0, 1.0));
    }

    vec3 litLinear = albedoRgb * lightTerm + (fullbrightAdditive ? emission : vec3(0.0));
    finalColor = vec4(tonemapDisplay(litLinear), albedoA);
}
