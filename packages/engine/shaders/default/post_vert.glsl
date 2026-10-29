#version 330

/// Fullscreen quad vertex shader shared by every screen pass.
///
/// Every post-process fragment shader is compiled against this file, so a pass can
/// rely on `fragTexCoord` spanning the screen and `fragColor` carrying the draw tint.
///
/// @used-by `post-push-shader`, the underwater effect, the scope mask and the loading crossfade.
/// @see post-push-shader

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
