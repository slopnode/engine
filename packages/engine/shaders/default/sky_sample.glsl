/// Sky sampling shared by `default/skybox_frag` and `default/sky_face_frag`.
///
/// Not a program on its own. The engine replaces the line `#include "SKY_SAMPLE"`
/// in a host shader with this file's text, which provides `sampleSky` and the
/// uniforms below. `skyMode` selects a solid color, a vertical gradient, a cube map
/// or a cylinder panorama, matching the sky block of a material.
///
/// @used-by Both sky fragment shaders.

/// 0 solid color, 1 cube map, 2 gradient, 3 cylinder panorama.
uniform int skyMode;
/// Color for mode 0.
uniform vec3 skySolidColor;
/// Cube map for mode 1.
uniform samplerCube skyCube;
/// Panorama texture for mode 3.
uniform sampler2D skyCylinder;
/// Horizontal offset of the panorama.
uniform float skyCylinderOffset;
/// Vertical scale of the panorama.
uniform float skyCylinderScale;
/// Horizontal repeats around the cylinder.
uniform int skyCylinderRepeat;
/// Fill color above the panorama.
uniform vec3 skyCylinderTopColor;
/// Fill color below the panorama.
uniform vec3 skyCylinderBottomColor;
/// Up to four gradient stops for mode 2.
uniform vec4 skyGradientColors[4];
/// Vertical position of each gradient stop.
uniform float skyGradientPositions[4];

vec3 sampleSkyGradient(float t)
{
    if (t <= skyGradientPositions[0]) {
        return skyGradientColors[0].rgb;
    }
    if (t >= skyGradientPositions[3]) {
        return skyGradientColors[3].rgb;
    }
    for (int i = 0; i < 3; ++i) {
        float edge0 = skyGradientPositions[i];
        float edge1 = skyGradientPositions[i + 1];
        if (t >= edge0 && t <= edge1) {
            float span = max(edge1 - edge0, 1e-5);
            float blend = (t - edge0) / span;
            return mix(skyGradientColors[i].rgb, skyGradientColors[i + 1].rgb, blend);
        }
    }
    return skyGradientColors[3].rgb;
}

// Mirrors UZDoom/GZDoom's sky dome: the texture only wraps the band within
// skyCylinderScale of the horizon. Beyond that band the sky is a flat color
// (the average of the texture's top/bottom rows) rather than a stretched or
// pinched sample, so there is no distortion at the poles.
vec3 sampleSkyCylinder(vec3 dir)
{
    vec3 n = normalize(dir);
    float yaw = atan(n.x, n.z);
    float repeat = max(float(skyCylinderRepeat), 1.0);
    float u = fract(yaw / (2.0 * 3.14159265359) * repeat + 0.5);
    float scale = max(skyCylinderScale, 0.05);
    float v = 0.5 - (n.y - skyCylinderOffset) / scale;

    if (v < 0.0) {
        return skyCylinderTopColor;
    }
    if (v > 1.0) {
        return skyCylinderBottomColor;
    }
    return texture(skyCylinder, vec2(u, v)).rgb;
}

vec3 sampleSky(vec3 worldDir, vec3 viewDir)
{
    if (skyMode == 0) {
        return skySolidColor;
    }
    if (skyMode == 1) {
        return texture(skyCube, normalize(worldDir)).rgb;
    }
    if (skyMode == 3) {
        return sampleSkyCylinder(worldDir);
    }
    float t = clamp(normalize(worldDir).y * 0.5 + 0.5, 0.0, 1.0);
    return sampleSkyGradient(t);
}
