#version 330

/// Third SMAA pass: blends each pixel with its neighbor using the weights.
///
/// Draws the weights from the second pass and reads the scene color through `colorTex` with bilinear filtering. Blending happens in approximate linear light.
///
/// Subpixel Morphological Antialiasing v2.8 by Jorge Jimenez, Jose I. Echevarria, Tiago Sousa and Diego Gutierrez (MIT), via the three.js port.
///
/// @used-by The engine when anti-aliasing is set to SMAA.
/// @see default/smaa_edges_frag default/smaa_weights_frag

in vec2 fragTexCoord;
in vec4 fragColor;

/// The blending weights from the second pass.
uniform sampler2D texture0;
/// The rendered scene.
uniform sampler2D colorTex;
/// Render target size in pixels.
uniform vec2 resolution;

out vec4 finalColor;

void main()
{
    vec2 texel = 1.0 / max(resolution, vec2(1.0));
    vec2 uv = fragTexCoord;
    vec4 offset1 = uv.xyxy + texel.xyxy * vec4(1.0, 0.0, 0.0, -1.0);

    vec4 a;
    a.xz = texture(texture0, uv).xz;
    a.y = texture(texture0, offset1.zw).g;
    a.w = texture(texture0, offset1.xy).a;

    if (dot(a, vec4(1.0)) < 1e-5) {
        finalColor = vec4(textureLod(colorTex, uv, 0.0).rgb, 1.0);
        return;
    }

    vec2 offset;
    offset.x = a.a > a.b ? a.a : -a.b;
    offset.y = a.g > a.r ? -a.g : a.r;

    if (abs(offset.x) > abs(offset.y)) {
        offset.y = 0.0;
    } else {
        offset.x = 0.0;
    }

    vec3 c = textureLod(colorTex, uv, 0.0).rgb;
    vec3 cop = textureLod(colorTex, uv + sign(offset) * texel, 0.0).rgb;
    float s = max(abs(offset.x), abs(offset.y));

    c = pow(c, vec3(2.2));
    cop = pow(cop, vec3(2.2));
    finalColor = vec4(pow(mix(c, cop, s), vec3(1.0 / 2.2)), 1.0);
}
