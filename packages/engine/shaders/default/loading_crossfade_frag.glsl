#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D snapshotTexture;
uniform float mixT;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec4 toColor = texture(texture0, fragTexCoord);
    vec2 snapshotUv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec4 fromColor = texture(snapshotTexture, snapshotUv);
    finalColor = mix(fromColor, toColor, clamp(mixT, 0.0, 1.0)) * colDiffuse * fragColor;
}
