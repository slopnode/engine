#version 330

/// Fast approximate anti-aliasing of the finished scene.
///
/// The engine runs this pass last on the scene stack when FXAA is selected under Graphics, so the HUD stays sharp.
/// The scene texture is sampled with bilinear filtering.
///
/// Adapted from the FXAA algorithm by NVIDIA, via the C# implementation by Jasper Flick and the GLSL port by Dave Hoskins as shipped with three.js (MIT).
///
/// @used-by The engine when anti-aliasing is set to FXAA.

in vec2 fragTexCoord;
in vec4 fragColor;

/// The rendered scene.
uniform sampler2D texture0;
/// Render target size in pixels.
uniform vec2 resolution;

out vec4 finalColor;

#define EDGE_STEP_COUNT 6
#define EDGE_GUESS 8.0
const float edgeSteps[EDGE_STEP_COUNT] = float[EDGE_STEP_COUNT](1.0, 1.5, 2.0, 2.0, 2.0, 4.0);

const float kContrastThreshold = 0.0312;
const float kRelativeThreshold = 0.063;
const float kSubpixelBlending = 1.0;

vec2 texel;

float lumaAt(vec2 uv)
{
    return dot(texture(texture0, uv).rgb, vec3(0.3, 0.59, 0.11));
}

float lumaAt(vec2 uv, float uOffset, float vOffset)
{
    return lumaAt(uv + texel * vec2(uOffset, vOffset));
}

struct Luma {
    float m, n, e, s, w;
    float ne, nw, se, sw;
    float highest, lowest, contrast;
};

Luma sampleNeighborhood(vec2 uv)
{
    Luma l;
    l.m = lumaAt(uv);
    l.n = lumaAt(uv, 0.0, 1.0);
    l.e = lumaAt(uv, 1.0, 0.0);
    l.s = lumaAt(uv, 0.0, -1.0);
    l.w = lumaAt(uv, -1.0, 0.0);
    l.ne = lumaAt(uv, 1.0, 1.0);
    l.nw = lumaAt(uv, -1.0, 1.0);
    l.se = lumaAt(uv, 1.0, -1.0);
    l.sw = lumaAt(uv, -1.0, -1.0);
    l.highest = max(max(max(max(l.n, l.e), l.s), l.w), l.m);
    l.lowest = min(min(min(min(l.n, l.e), l.s), l.w), l.m);
    l.contrast = l.highest - l.lowest;
    return l;
}

float pixelBlendFactor(Luma l)
{
    float f = 2.0 * (l.n + l.e + l.s + l.w);
    f += l.ne + l.nw + l.se + l.sw;
    f *= 1.0 / 12.0;
    f = abs(f - l.m);
    f = clamp(f / l.contrast, 0.0, 1.0);
    float blend = smoothstep(0.0, 1.0, f);
    return blend * blend * kSubpixelBlending;
}

struct Edge {
    bool isHorizontal;
    float pixelStep;
    float oppositeLuma;
    float gradient;
};

Edge determineEdge(Luma l)
{
    Edge e;
    float horizontal =
        abs(l.n + l.s - 2.0 * l.m) * 2.0 +
        abs(l.ne + l.se - 2.0 * l.e) +
        abs(l.nw + l.sw - 2.0 * l.w);
    float vertical =
        abs(l.e + l.w - 2.0 * l.m) * 2.0 +
        abs(l.ne + l.nw - 2.0 * l.n) +
        abs(l.se + l.sw - 2.0 * l.s);
    e.isHorizontal = horizontal >= vertical;

    float pLuma = e.isHorizontal ? l.n : l.e;
    float nLuma = e.isHorizontal ? l.s : l.w;
    float pGradient = abs(pLuma - l.m);
    float nGradient = abs(nLuma - l.m);

    e.pixelStep = e.isHorizontal ? texel.y : texel.x;
    if (pGradient < nGradient) {
        e.pixelStep = -e.pixelStep;
        e.oppositeLuma = nLuma;
        e.gradient = nGradient;
    } else {
        e.oppositeLuma = pLuma;
        e.gradient = pGradient;
    }
    return e;
}

float edgeBlendFactor(Luma l, Edge e, vec2 uv)
{
    vec2 uvEdge = uv;
    vec2 edgeStep;
    if (e.isHorizontal) {
        uvEdge.y += e.pixelStep * 0.5;
        edgeStep = vec2(texel.x, 0.0);
    } else {
        uvEdge.x += e.pixelStep * 0.5;
        edgeStep = vec2(0.0, texel.y);
    }

    float edgeLuma = (l.m + e.oppositeLuma) * 0.5;
    float gradientThreshold = e.gradient * 0.25;

    vec2 puv = uvEdge + edgeStep * edgeSteps[0];
    float pDelta = lumaAt(puv) - edgeLuma;
    bool pAtEnd = abs(pDelta) >= gradientThreshold;
    for (int i = 1; i < EDGE_STEP_COUNT && !pAtEnd; i++) {
        puv += edgeStep * edgeSteps[i];
        pDelta = lumaAt(puv) - edgeLuma;
        pAtEnd = abs(pDelta) >= gradientThreshold;
    }
    if (!pAtEnd) {
        puv += edgeStep * EDGE_GUESS;
    }

    vec2 nuv = uvEdge - edgeStep * edgeSteps[0];
    float nDelta = lumaAt(nuv) - edgeLuma;
    bool nAtEnd = abs(nDelta) >= gradientThreshold;
    for (int i = 1; i < EDGE_STEP_COUNT && !nAtEnd; i++) {
        nuv -= edgeStep * edgeSteps[i];
        nDelta = lumaAt(nuv) - edgeLuma;
        nAtEnd = abs(nDelta) >= gradientThreshold;
    }
    if (!nAtEnd) {
        nuv -= edgeStep * EDGE_GUESS;
    }

    float pDistance;
    float nDistance;
    if (e.isHorizontal) {
        pDistance = puv.x - uv.x;
        nDistance = uv.x - nuv.x;
    } else {
        pDistance = puv.y - uv.y;
        nDistance = uv.y - nuv.y;
    }

    float shortestDistance;
    bool deltaSign;
    if (pDistance <= nDistance) {
        shortestDistance = pDistance;
        deltaSign = pDelta >= 0.0;
    } else {
        shortestDistance = nDistance;
        deltaSign = nDelta >= 0.0;
    }

    if (deltaSign == (l.m - edgeLuma >= 0.0)) {
        return 0.0;
    }
    return 0.5 - shortestDistance / (pDistance + nDistance);
}

void main()
{
    texel = 1.0 / max(resolution, vec2(1.0));
    vec2 uv = fragTexCoord;

    Luma l = sampleNeighborhood(uv);
    float threshold = max(kContrastThreshold, kRelativeThreshold * l.highest);
    if (l.contrast < threshold) {
        finalColor = vec4(texture(texture0, uv).rgb, 1.0);
        return;
    }

    float pixelBlend = pixelBlendFactor(l);
    Edge edge = determineEdge(l);
    float edgeBlend = edgeBlendFactor(l, edge, uv);
    float finalBlend = max(pixelBlend, edgeBlend);

    if (edge.isHorizontal) {
        uv.y += edge.pixelStep * finalBlend;
    } else {
        uv.x += edge.pixelStep * finalBlend;
    }
    finalColor = vec4(texture(texture0, uv).rgb, 1.0);
}
