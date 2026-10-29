#version 330

/// Lights first-person geometry from the probe at the eye plus a key light and a rim.
///
/// `probeRgb` and `ambient` set the base lighting. `keyDir` with `keyStrength` adds a
/// directional term, and `rimStrength` brightens edges facing away from the viewer so
/// the weapon separates from the scene behind it.
///
/// @used-by Geometry attached through `fp-attach-geo`.
/// @see fp-attach-geo fp-set-shading fp-set-rad-tint

in vec3 fragNormal;
in vec2 fragTexCoord;
in vec4 fragColor;

/// Albedo texture.
uniform sampler2D texture0;
/// Material base color.
uniform vec4 colDiffuse;
/// Lighting probe sample at the eye.
uniform vec3 probeRgb;
/// Ambient light scale.
uniform float ambient;
/// Key light direction.
uniform vec3 keyDir;
/// Key light weight.
uniform float keyStrength;
/// Rim light weight.
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
