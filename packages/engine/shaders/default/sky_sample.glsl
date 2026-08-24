uniform int skyMode;
uniform vec3 skySolidColor;
uniform samplerCube skyCube;
uniform sampler2D skyCylinder;
uniform float skyCylinderOffset;
uniform float skyCylinderScale;
uniform int skyCylinderRepeat;
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

vec3 sampleSkyCylinderCap(float v)
{
    vec3 c0 = texture(skyCylinder, vec2(0.0, v)).rgb;
    vec3 c1 = texture(skyCylinder, vec2(0.25, v)).rgb;
    vec3 c2 = texture(skyCylinder, vec2(0.5, v)).rgb;
    vec3 c3 = texture(skyCylinder, vec2(0.75, v)).rgb;
    return (c0 + c1 + c2 + c3) * 0.25;
}

vec3 sampleSkyCylinder(vec3 dir)
{
    vec3 n = normalize(dir);
    float yaw = atan(n.x, n.z);
    float repeat = max(float(skyCylinderRepeat), 1.0);
    float u = yaw / (2.0 * 3.14159265359) * repeat + 0.5;
    float scale = max(skyCylinderScale, 0.05);
    float v = clamp(0.5 - (n.y - skyCylinderOffset) / scale, 0.0, 1.0);

    float u1 = fract(u);
    float u2 = fract(u + 0.5);
    vec3 c1 = texture(skyCylinder, vec2(u1, v)).rgb;
    vec3 c2 = texture(skyCylinder, vec2(u2, v)).rgb;
    float seamBlend = smoothstep(0.0, 1.0, abs(u1 - 0.5) * 2.0);
    vec3 texColor = mix(c1, c2, seamBlend);

    vec3 capColor = sampleSkyCylinderCap(v);
    float poleFade = smoothstep(0.35, 0.75, abs(n.y));
    return mix(texColor, capColor, poleFade);
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
