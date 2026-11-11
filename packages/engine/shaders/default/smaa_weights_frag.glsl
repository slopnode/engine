#version 330

/// Second SMAA pass: turns edges into blending weights.
///
/// Searches along each edge for its ends and looks the crossing pattern up in the precomputed area texture to get how much of each neighbor to blend in.
///
/// Subpixel Morphological Antialiasing v2.8 by Jorge Jimenez, Jose I. Echevarria, Tiago Sousa and Diego Gutierrez (MIT), in the 1x Medium preset, via the three.js port.
///
/// @used-by The engine when anti-aliasing is set to SMAA.
/// @see default/smaa_edges_frag default/smaa_blend_frag

in vec2 fragTexCoord;
in vec4 fragColor;

/// The edge flags from the first pass.
uniform sampler2D texture0;
/// The area lookup texture, `fx/smaa_area`.
uniform sampler2D areaTex;
/// The search lookup texture, `fx/smaa_search`.
uniform sampler2D searchTex;
/// Render target size in pixels.
uniform vec2 resolution;

out vec4 finalColor;

#define SMAA_MAX_SEARCH_STEPS 8
#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / vec2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)

vec2 texel;

float searchLength(vec2 e, float bias, float scale)
{
    e.r = bias + e.r * scale;
    return 255.0 * textureLod(searchTex, e, 0.0).r;
}

float searchXLeft(vec2 coord, float end)
{
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = textureLod(texture0, coord, 0.0).rg;
        coord -= vec2(2.0, 0.0) * texel;
        if (!(coord.x > end && e.g > 0.8281 && e.r == 0.0)) {
            break;
        }
    }
    coord.x += 0.25 * texel.x;
    coord.x += texel.x;
    coord.x += 2.0 * texel.x;
    coord.x -= texel.x * searchLength(e, 0.0, 0.5);
    return coord.x;
}

float searchXRight(vec2 coord, float end)
{
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = textureLod(texture0, coord, 0.0).rg;
        coord += vec2(2.0, 0.0) * texel;
        if (!(coord.x < end && e.g > 0.8281 && e.r == 0.0)) {
            break;
        }
    }
    coord.x -= 0.25 * texel.x;
    coord.x -= texel.x;
    coord.x -= 2.0 * texel.x;
    coord.x += texel.x * searchLength(e, 0.5, 0.5);
    return coord.x;
}

float searchYUp(vec2 coord, float end)
{
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = textureLod(texture0, coord, 0.0).rg;
        coord += vec2(0.0, 2.0) * texel;
        if (!(coord.y > end && e.r > 0.8281 && e.g == 0.0)) {
            break;
        }
    }
    coord.y -= 0.25 * texel.y;
    coord.y -= texel.y;
    coord.y -= 2.0 * texel.y;
    coord.y += texel.y * searchLength(e.gr, 0.0, 0.5);
    return coord.y;
}

float searchYDown(vec2 coord, float end)
{
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        e = textureLod(texture0, coord, 0.0).rg;
        coord -= vec2(0.0, 2.0) * texel;
        if (!(coord.y < end && e.r > 0.8281 && e.g == 0.0)) {
            break;
        }
    }
    coord.y += 0.25 * texel.y;
    coord.y += texel.y;
    coord.y += 2.0 * texel.y;
    coord.y -= texel.y * searchLength(e.gr, 0.5, 0.5);
    return coord.y;
}

vec2 areaLookup(vec2 dist, float e1, float e2)
{
    vec2 coord = float(SMAA_AREATEX_MAX_DISTANCE) * round(4.0 * vec2(e1, e2)) + dist;
    coord = SMAA_AREATEX_PIXEL_SIZE * coord + (0.5 * SMAA_AREATEX_PIXEL_SIZE);
    return textureLod(areaTex, coord, 0.0).rg;
}

void main()
{
    texel = 1.0 / max(resolution, vec2(1.0));
    vec2 uv = fragTexCoord;
    vec2 pixcoord = uv / texel;

    vec4 offset0 = uv.xyxy + texel.xyxy * vec4(-0.25, 0.125, 1.25, 0.125);
    vec4 offset1 = uv.xyxy + texel.xyxy * vec4(-0.125, 0.25, -0.125, -1.25);
    vec4 offset2 = vec4(offset0.xz, offset1.yw) +
        vec4(-2.0, 2.0, -2.0, 2.0) * texel.xxyy * float(SMAA_MAX_SEARCH_STEPS);

    vec4 weights = vec4(0.0);
    vec2 e = texture(texture0, uv).rg;

    if (e.g > 0.0) {
        vec2 d;
        vec2 coords;
        coords.x = searchXLeft(offset0.xy, offset2.x);
        coords.y = offset1.y;
        d.x = coords.x;
        float e1 = textureLod(texture0, coords, 0.0).r;

        coords.x = searchXRight(offset0.zw, offset2.y);
        d.y = coords.x;

        d = d / texel.x - pixcoord.x;
        vec2 sqrtD = sqrt(abs(d));

        coords.y -= texel.y;
        float e2 = textureLod(texture0, coords + vec2(1.0, 0.0) * texel, 0.0).r;

        weights.rg = areaLookup(sqrtD, e1, e2);
    }

    if (e.r > 0.0) {
        vec2 d;
        vec2 coords;
        coords.y = searchYUp(offset1.xy, offset2.z);
        coords.x = offset0.x;
        d.x = coords.y;
        float e1 = textureLod(texture0, coords, 0.0).g;

        coords.y = searchYDown(offset1.zw, offset2.w);
        d.y = coords.y;

        d = d / texel.y - pixcoord.y;
        vec2 sqrtD = sqrt(abs(d));

        coords.y -= texel.y;
        float e2 = textureLod(texture0, coords + vec2(0.0, 1.0) * texel, 0.0).g;

        weights.ba = areaLookup(sqrtD, e1, e2);
    }

    finalColor = weights;
}
