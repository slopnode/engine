#version 330

in vec3 fragWorldDir;
in vec3 fragViewDir;

out vec4 finalColor;

#include "SKY_SAMPLE"

void main()
{
    finalColor = vec4(sampleSky(fragWorldDir, fragViewDir), 1.0);
}
