#version 330

/// Brush edit preview vertices with optional per-vertex color and a fixed-light faux shade.
///
/// @used-by slopmap's edit preview, paired with `tools/edit_preview_frag`.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
/// 1 darkens faces by their angle to a fixed light, 0 leaves them flat.
uniform int fauxShade;
/// 1 multiplies in the per-vertex color, 0 ignores it.
uniform int useVertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    vec4 color = useVertexColor == 1 ? vertexColor : vec4(1.0);
    if (fauxShade == 1) {
        float len = length(vertexNormal);
        vec3 n = len > 1e-6 ? vertexNormal / len : vec3(0.0, 1.0, 0.0);
        float ndotl = clamp(dot(n, normalize(vec3(0.45, 0.85, 0.35))), 0.55, 1.0);
        color.rgb *= ndotl;
    }
    fragTexCoord = vertexTexCoord;
    fragColor = color;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
