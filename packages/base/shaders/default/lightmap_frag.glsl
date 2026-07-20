#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec4 colDiffuse;
uniform vec4 colSpecular;
uniform int useLightmap;

const int MAX_DYN_LIGHTS = 8;

uniform int dynLightCount;
uniform vec4 dynLightPosRange[MAX_DYN_LIGHTS];
uniform vec4 dynLightColorIntensity[MAX_DYN_LIGHTS];
uniform vec4 dynLightDirCone[MAX_DYN_LIGHTS];
uniform vec4 dynLightMeta[MAX_DYN_LIGHTS];

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

vec3 evalOneLight(int i, vec3 worldPos)
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
    float t = dist / range;
    float atten = max(0.0, 1.0 - t * t);
    atten *= atten;

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

    vec3 radiance = dynLightColorIntensity[i].rgb * dynLightColorIntensity[i].a;
    return radiance * (atten * spot);
}

vec3 evalDynamicLights(vec3 worldPos)
{
    vec3 total = vec3(0.0);
    int count = clamp(dynLightCount, 0, MAX_DYN_LIGHTS);
    if (count > 0) total += evalOneLight(0, worldPos);
    if (count > 1) total += evalOneLight(1, worldPos);
    if (count > 2) total += evalOneLight(2, worldPos);
    if (count > 3) total += evalOneLight(3, worldPos);
    if (count > 4) total += evalOneLight(4, worldPos);
    if (count > 5) total += evalOneLight(5, worldPos);
    if (count > 6) total += evalOneLight(6, worldPos);
    if (count > 7) total += evalOneLight(7, worldPos);
    return total;
}

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 baked = useLightmap != 0 ? texture(texture1, fragTexCoord2).rgb : vec3(1.0);
    vec3 dynamic = evalDynamicLights(fragPosition);
    vec3 emit = colSpecular.rgb;
    finalColor = vec4(albedo.rgb * (baked + dynamic) + emit, albedo.a);
}
