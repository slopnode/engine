#version 330

in vec3 fragWorldPos;

uniform vec3 cameraPos;
uniform mat4 matViewRot;

out vec4 finalColor;

#include "SKY_SAMPLE"

void main()
{
    vec3 worldDir = fragWorldPos - cameraPos;
    vec3 viewDir = (matViewRot * vec4(worldDir, 0.0)).xyz;
    finalColor = vec4(sampleSky(worldDir, viewDir), 1.0);
}
