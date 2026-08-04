#version 330

in vec3 vertexPosition;

uniform mat4 matProjection;
uniform mat4 matViewRot;

out vec3 fragWorldDir;
out vec3 fragViewDir;

void main()
{
    fragWorldDir = vertexPosition;
    fragViewDir = (matViewRot * vec4(vertexPosition, 0.0)).xyz;
    vec4 pos = matProjection * vec4(fragViewDir, 1.0);
    gl_Position = pos.xyww;
}
