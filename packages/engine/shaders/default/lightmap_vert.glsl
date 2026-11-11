#version 330

/// Transforms world brush vertices and passes both UV sets to the fragment stage.
///
/// @used-by World brushes, paired with `default/lightmap_frag`.

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
/// Lightmap atlas coordinates, the second UV set written by sloprad.
in vec2 vertexTexCoord2;
in vec4 vertexColor;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 boneMatrices[128];
uniform int skinned;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;

#include "MODEL_DEFORM"

void main()
{
    vec4 position = vec4(vertexPosition, 1.0);
    vec4 normal = vec4(vertexNormal, 0.0);
    if (skinned == 1) {
        mat4 skin =
            vertexBoneWeights.x * boneMatrices[int(vertexBoneIndices.x)] +
            vertexBoneWeights.y * boneMatrices[int(vertexBoneIndices.y)] +
            vertexBoneWeights.z * boneMatrices[int(vertexBoneIndices.z)] +
            vertexBoneWeights.w * boneMatrices[int(vertexBoneIndices.w)];
        position = skin * position;
        normal = vec4(transpose(inverse(mat3(skin))) * vertexNormal, 0.0);
    }
    vec3 deformedPosition = position.xyz;
    vec3 deformedNormal = normal.xyz;
    modelDeform(deformedPosition, deformedNormal, vertexTexCoord, vertexBoneIndices, vertexBoneWeights);
    position = vec4(deformedPosition, 1.0);
    normal = vec4(deformedNormal, 0.0);
    vec4 world = matModel * position;
    fragPosition = world.xyz;
    fragNormal = normalize((matNormal * normal).xyz);
    fragTexCoord = vertexTexCoord;
    fragTexCoord2 = vertexTexCoord2;
    fragColor = vertexColor;
    gl_Position = mvp * position;
}
