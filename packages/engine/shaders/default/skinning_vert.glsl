#version 330

/// Skins mesh vertices with up to 128 bone matrices.
///
/// @used-by Skeletal geometry, paired with `default/skinning_frag`.

#define MAX_BONE_NUM 128

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
in vec3 vertexNormal;
/// Four bone indices per vertex.
in vec4 vertexBoneIndices;
/// Matching weights for the four bones.
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matNormal;
/// Bone transforms indexed by vertexBoneIndices.
uniform mat4 boneMatrices[MAX_BONE_NUM];

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

#include "MODEL_DEFORM"

void main()
{
    int boneIndex0 = int(vertexBoneIndices.x);
    int boneIndex1 = int(vertexBoneIndices.y);
    int boneIndex2 = int(vertexBoneIndices.z);
    int boneIndex3 = int(vertexBoneIndices.w);

    vec4 skinnedPosition =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexPosition, 1.0));

    vec4 skinnedNormal =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexNormal, 0.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexNormal, 0.0));
    skinnedNormal.w = 0.0;

    vec3 deformedPosition = skinnedPosition.xyz;
    vec3 deformedNormal = skinnedNormal.xyz;
    modelDeform(deformedPosition, deformedNormal, vertexTexCoord, vertexBoneIndices, vertexBoneWeights);
    skinnedPosition = vec4(deformedPosition, 1.0);
    skinnedNormal = vec4(deformedNormal, 0.0);

    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*skinnedNormal));

    gl_Position = mvp*skinnedPosition;
}
