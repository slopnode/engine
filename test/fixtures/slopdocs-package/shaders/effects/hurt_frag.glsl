#version 330

/// Tints the scene red by `strength`, for a hurt flash.
///
/// Pushed with `post-push-shader` against `default/post_vert`.
///
/// @used-by Pushed on the scene stack from the player lifecycle script.
/// @see post-push-shader post-set-float

in vec2 fragTexCoord;
in vec4 fragColor;

/// The rendered scene.
uniform sampler2D texture0;
uniform vec4 colDiffuse;
/// Blend toward red, 0 to 1.
uniform float strength;

out vec4 finalColor;

void main()
{
    vec4 scene = texture(texture0, fragTexCoord);
    vec3 red = mix(scene.rgb, vec3(1.0, 0.1, 0.1), strength);
    finalColor = vec4(red, scene.a) * colDiffuse * fragColor;
}
