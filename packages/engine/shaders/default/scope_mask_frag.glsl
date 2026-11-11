#version 330

/// Composites a secondary eye view through a mask texture.
///
/// @used-by `hud-draw-eye` when a mask texture is given.
/// @see hud-draw-eye extra-eye-configure!

in vec2 fragTexCoord;
in vec4 fragColor;

/// Secondary eye render.
uniform sampler2D texture0;
/// Its red channel multiplies the eye view's alpha.
uniform sampler2D maskTexture;
/// Draw tint.
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec4 sceneColor = texture(texture0, fragTexCoord);
    float maskAlpha = texture(maskTexture, fragTexCoord).r;
    finalColor = vec4(sceneColor.rgb, sceneColor.a * maskAlpha) * colDiffuse * fragColor;
}
