#version 330

/// First SMAA pass: finds color edges in the scene.
///
/// Writes left and top edge flags to the red and green channels. Pixels without an edge write nothing, so the target must be cleared first.
///
/// Subpixel Morphological Antialiasing v2.8 by Jorge Jimenez, Jose I. Echevarria, Tiago Sousa and Diego Gutierrez (MIT), in the 1x Medium preset with color edge detection, via the three.js port.
///
/// @used-by The engine when anti-aliasing is set to SMAA.
/// @see default/smaa_weights_frag default/smaa_blend_frag

in vec2 fragTexCoord;
in vec4 fragColor;

/// The rendered scene.
uniform sampler2D texture0;
/// Render target size in pixels.
uniform vec2 resolution;

out vec4 finalColor;

const float kThreshold = 0.1;

float maxChannel(vec3 v)
{
    return max(max(v.r, v.g), v.b);
}

void main()
{
    vec2 texel = 1.0 / max(resolution, vec2(1.0));
    vec2 uv = fragTexCoord;

    vec4 offset0 = uv.xyxy + texel.xyxy * vec4(-1.0, 0.0, 0.0, 1.0);
    vec4 offset1 = uv.xyxy + texel.xyxy * vec4(1.0, 0.0, 0.0, -1.0);
    vec4 offset2 = uv.xyxy + texel.xyxy * vec4(-2.0, 0.0, 0.0, 2.0);

    vec4 delta;
    vec3 c = texture(texture0, uv).rgb;
    delta.x = maxChannel(abs(c - texture(texture0, offset0.xy).rgb));
    delta.y = maxChannel(abs(c - texture(texture0, offset0.zw).rgb));

    vec2 edges = step(vec2(kThreshold), delta.xy);
    if (dot(edges, vec2(1.0)) == 0.0) {
        discard;
    }

    delta.z = maxChannel(abs(c - texture(texture0, offset1.xy).rgb));
    delta.w = maxChannel(abs(c - texture(texture0, offset1.zw).rgb));
    float maxDelta = max(max(max(delta.x, delta.y), delta.z), delta.w);

    delta.z = maxChannel(abs(c - texture(texture0, offset2.xy).rgb));
    delta.w = maxChannel(abs(c - texture(texture0, offset2.zw).rgb));
    maxDelta = max(max(maxDelta, delta.z), delta.w);

    edges *= step(0.5 * maxDelta, delta.xy);
    finalColor = vec4(edges, 0.0, 0.0);
}
