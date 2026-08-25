uniform int skyMode;
uniform vec3 skySolidColor;
uniform samplerCube skyCube;
uniform sampler2D skyCylinder;
uniform float skyCylinderOffset;
uniform float skyCylinderScale;
uniform int skyCylinderRepeat;
uniform vec3 skyCylinderTopColor;
uniform vec3 skyCylinderBottomColor;
uniform vec4 skyGradientColors[4];
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
