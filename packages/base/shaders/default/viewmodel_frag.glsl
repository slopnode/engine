#version 330

in vec3 fragNormal;
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 probeRgb;
uniform float ambient;
uniform vec3 keyDir;
uniform float keyStrength;
uniform float rimStrength;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 albedo = texel.rgb * colDiffuse.rgb * fragColor.rgb;
    float alpha = texel.a * colDiffuse.a * fragColor.a;

    vec3 n = normalize(fragNormal);
    n.x = -n.x;

    vec3 L = normalize(keyDir);
    float ndotl = max(dot(n, L), 0.0);
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);

    vec3 lighting = vec3(ambient) + keyStrength * ndotl + rimStrength * rim;
    vec3 color = albedo * probeRgb * lighting;
    finalColor = vec4(color, alpha);
}
