uniform int skyMode;
uniform vec3 skySolidColor;
uniform samplerCube skyCube;
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

vec3 sampleSky(vec3 worldDir, vec3 viewDir)
{
    if (skyMode == 0) {
        return skySolidColor;
    }
    if (skyMode == 1) {
        return texture(skyCube, normalize(worldDir)).rgb;
    }
    float t = clamp(normalize(worldDir).y * 0.5 + 0.5, 0.0, 1.0);
    return sampleSkyGradient(t);
}
