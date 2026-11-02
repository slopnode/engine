#version 330

/// Transforms vertices for a dynamic light's shadow map pass.
///
/// @used-by Shadow map passes for dynamic lights, paired with `default/shadow_depth_frag`.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 boneMatrices[128];
uniform int skinned;

out vec2 fragTexCoord;

#include "MODEL_DEFORM"

void main()
{
    vec4 position = vec4(vertexPosition, 1.0);
    if (skinned == 1) {
        mat4 skin =
            vertexBoneWeights.x * boneMatrices[int(vertexBoneIndices.x)] +
            vertexBoneWeights.y * boneMatrices[int(vertexBoneIndices.y)] +
            vertexBoneWeights.z * boneMatrices[int(vertexBoneIndices.z)] +
            vertexBoneWeights.w * boneMatrices[int(vertexBoneIndices.w)];
        position = skin * position;
    }
    vec3 deformedPosition = position.xyz;
    vec3 deformedNormal = vec3(0.0, 1.0, 0.0);
    modelDeform(deformedPosition, deformedNormal, vertexTexCoord, vertexBoneIndices, vertexBoneWeights);
    position = vec4(deformedPosition, 1.0);
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * position;
}
