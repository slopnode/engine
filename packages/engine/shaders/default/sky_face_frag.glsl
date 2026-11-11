#version 330

/// Samples the sky through a sky-flagged brush face.
///
/// The direction is from `cameraPos` to the face's world position, so the face acts
/// as a window onto the sky. The sampling code and its uniforms come from
/// `default/sky_sample`.
///
/// @used-by Brush faces whose material has the sky flag.

in vec3 fragWorldPos;

/// Eye position in world space.
uniform vec3 cameraPos;
/// Camera rotation, passed to the sky sampler.
uniform mat4 matViewRot;

out vec4 finalColor;

#include "SKY_SAMPLE"

void main()
{
    vec3 worldDir = fragWorldPos - cameraPos;
    vec3 viewDir = (matViewRot * vec4(worldDir, 0.0)).xyz;
    finalColor = vec4(sampleSky(worldDir, viewDir), 1.0);
}
