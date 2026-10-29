#version 330

/// Transforms vertices for a dynamic light's shadow map pass.
///
/// @used-by Shadow map passes for dynamic lights, paired with `default/shadow_depth_frag`.

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;

out vec2 fragTexCoord;

void main()
{
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
