#version 330

/// Transforms world brush vertices and passes both UV sets to the fragment stage.
///
/// @used-by World brushes, paired with `default/lightmap_frag`.

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
/// Lightmap atlas coordinates, the second UV set written by sloprad.
in vec2 vertexTexCoord2;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;

void main()
{
    vec4 world = matModel * vec4(vertexPosition, 1.0);
    fragPosition = world.xyz;
    fragNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    fragTexCoord = vertexTexCoord;
    fragTexCoord2 = vertexTexCoord2;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
