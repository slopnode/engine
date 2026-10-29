#version 330

/// Passes grid plane vertices through with their world position.
///
/// @used-by slopmap's preview grid, paired with `tools/grid_frag`.

in vec3 vertexPosition;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragWorldPos;

void main()
{
    vec4 world = matModel * vec4(vertexPosition, 1.0);
    fragWorldPos = world.xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
