#version 330

/// Projects the skybox cube and derives a world and a view direction per vertex.
///
/// @used-by The sky drawn behind the world, paired with `default/skybox_frag`.

in vec3 vertexPosition;

/// Camera projection matrix.
uniform mat4 matProjection;
/// Camera rotation without translation.
uniform mat4 matViewRot;

out vec3 fragWorldDir;
out vec3 fragViewDir;

void main()
{
    fragViewDir = (matViewRot * vec4(vertexPosition, 0.0)).xyz;
    fragWorldDir = transpose(mat3(matViewRot)) * fragViewDir;
    vec4 pos = matProjection * vec4(fragViewDir, 1.0);
    gl_Position = pos.xyww;
}
