#version 330

in vec3 vertexPosition;

uniform mat4 matProjection;
uniform mat4 matViewRot;

out vec3 fragWorldDir;
out vec3 fragViewDir;

void main()
{
    fragViewDir = (matViewRot * vec4(vertexPosition, 0.0)).xyz;
    mat3 viewToWorld = transpose(mat3(matViewRot));
    fragWorldDir = viewToWorld * vertexPosition;
    vec4 pos = matProjection * vec4(fragViewDir, 1.0);
    gl_Position = pos.xyww;
}
