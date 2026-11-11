/// Vertex deform hook for models, spliced in at `#include "MODEL_DEFORM"` and called after skinning, in model space.
///
/// @used-by `default/lightmap_vert`, `default/skinning_vert` and `default/shadow_depth_vert`. A package overrides this file to bend models.

void modelDeform(inout vec3 position, inout vec3 normal, vec2 texCoord, vec4 boneIndices, vec4 boneWeights)
{
}
