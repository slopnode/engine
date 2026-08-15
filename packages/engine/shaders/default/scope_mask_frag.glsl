#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D maskTexture;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec4 sceneColor = texture(texture0, fragTexCoord);
    float maskAlpha = texture(maskTexture, fragTexCoord).r;
    finalColor = vec4(sceneColor.rgb, sceneColor.a * maskAlpha) * colDiffuse * fragColor;
}
