#version 330

/// Crossfades a snapshot of the previous frame into the new scene during a map load.
///
/// @used-by The map load transition.

in vec2 fragTexCoord;
in vec4 fragColor;

/// The new scene.
uniform sampler2D texture0;
/// Snapshot of the last frame before the load.
uniform sampler2D snapshotTexture;
/// Blend factor from 0 for the snapshot to 1 for the scene.
uniform float mixT;
/// Draw tint.
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec4 toColor = texture(texture0, fragTexCoord);
    vec2 snapshotUv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    vec4 fromColor = texture(snapshotTexture, snapshotUv);
    finalColor = mix(fromColor, toColor, clamp(mixT, 0.0, 1.0)) * colDiffuse * fragColor;
}
