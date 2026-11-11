#version 330

/// Transforms first-person attachment geometry and passes its normal in world space.
///
/// @used-by Geometry attached to first-person sockets, paired with `default/viewmodel_frag`.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 boneMatrices[128];
uniform int skinned;

out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragColor;

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
    mat3 normalMatrix = mat3(matModel);
    fragNormal = normalize(normalMatrix * normal.xyz);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * position;
}
