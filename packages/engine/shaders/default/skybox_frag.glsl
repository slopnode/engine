#version 330

/// Samples the sky for the direction through each pixel of the skybox.
///
/// The sampling code and its uniforms come from `default/sky_sample`, substituted
/// for the include line when the shader is loaded.
///
/// @used-by The sky drawn behind the world.

in vec3 fragWorldDir;
in vec3 fragViewDir;

out vec4 finalColor;

#include "SKY_SAMPLE"

void main()
{
    finalColor = vec4(sampleSky(fragWorldDir, fragViewDir), 1.0);
}
