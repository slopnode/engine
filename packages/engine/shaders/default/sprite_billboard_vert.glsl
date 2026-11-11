#version 330

/// Passes billboard quad vertices through with their texture coordinates.
///
/// @used-by World sprites and first-person sprites, paired with `default/sprite_billboard_frag`.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
