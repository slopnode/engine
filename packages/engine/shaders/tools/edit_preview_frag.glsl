#version 330

/// Brush edit preview: albedo texture times material tint times the shaded vertex color.
///
/// @used-by slopmap's edit preview, paired with `tools/edit_preview_vert`.

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    finalColor = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
}
