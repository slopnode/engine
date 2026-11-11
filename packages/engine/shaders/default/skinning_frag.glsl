#version 330

/// Textured, vertex-colored output for skinned meshes.
///
/// @used-by Skeletal geometry.

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

/// Albedo texture.
uniform sampler2D texture0;
/// Material base color.
uniform vec4 colDiffuse;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    finalColor = texelColor*colDiffuse*fragColor;
}
