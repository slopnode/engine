#version 430

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const float kPi = 3.14159265358979323846;
const float kMinEmitterContrib = 1e-5;
const float kMinCastLuminance = 0.03;
const int kLightKindPoint = 0;
const int kLightKindSpot = 1;
const int kLightKindSun = 2;
const float kEmitterNormalOffset = 0.02;

struct Luxel {
    float px;
    float py;
    float pz;
    int covered;
    float nx;
    float ny;
    float nz;
    int faceIndex;
    float ir;
    float ig;
    float ib;
    int leafIndex;
    float sunIr;
    float sunIg;
    float sunIb;
    int pad0;
};

struct EmissiveFace {
    float uAxisX;
    float uAxisY;
    float uAxisZ;
    float planeD;
    float vAxisX;
    float vAxisY;
    float vAxisZ;
    float uMin;
    float uMax;
    float vMin;
    float vMax;
    float nx;
    float ny;
    float nz;
    float area;
    int faceIndex;
    int interiorLeaf;
    int gridWidth;
    int gridHeight;
    int gridOffset;
    int directSampleOffset;
    float peakR;
    float peakG;
    float peakB;
    float castRange;
    float aabbMinX;
    float aabbMinY;
    float aabbMinZ;
    float pad3;
    float aabbMaxX;
    float aabbMaxY;
    float aabbMaxZ;
    float pad4;
};

struct GridSample {
    float r;
    float g;
    float b;
    float pad;
};

// One stratified direct-light sample, precomputed CPU-side per emissive face from the fine
// emission grid (see EmitterDirectSample in radiosity_emitters.hpp / bakeRadiosity()'s
// collection loop) so this shader's gather pass finds real content even for a mask feature
// much smaller than emitterDirectSamples^2 fixed query points would otherwise resolve.
struct DirectSample {
    float u;
    float v;
    float r;
    float g;
    float b;
    float pad0;
    float pad1;
    float pad2;
};

struct Light {
    float px;
    float py;
    float pz;
    int kind;
    float dx;
    float dy;
    float dz;
    float intensity;
    float cr;
    float cg;
    float cb;
    float range;
    float coneAngle;
    int leafIndex;
    float pad1;
    float pad2;
};

struct BvhNode {
    float minx;
    float miny;
    float minz;
    int left;
    float maxx;
    float maxy;
    float maxz;
    int right;
    int firstPrim;
    int primCount;
    int pad0;
    int pad1;
};

struct BvhPrim {
    float v0x;
    float v0y;
    float v0z;
    float pad0;
    float v1x;
    float v1y;
    float v1z;
    float pad1;
    float v2x;
    float v2y;
    float v2z;
    float pad2;
    float nx;
    float ny;
    float nz;
    int faceIndex;
};

struct EmitterBvhPrim {
    float minx;
    float miny;
    float minz;
    float pad0;
    float maxx;
    float maxy;
    float maxz;
    int emitterIndex;
    int pad1;
    int pad2;
    int pad3;
    float pad4;
};

struct Params {
    int luxelCount;
    int emitterCount;
    int lightCount;
    int bvhRoot;
    int luxelOffset;
    int luxelBatch;
    int emitterBvhRoot;
    float emitterQueryRadius;
    int lightOffset;
    int lightBatch;
    int leafCount;
    int wordsPerRow;
    float directWrap;
    float coplanarFill;
    float coplanarSoft;
    float minDist2;
    int emitterDirectSamples;
    int emissionGridFloats;
    int sunRayCount;
    float sunAngularSpread;
    float sunLeakThreshold;
    int faceCount;
    int materialRectCount;
    int alphaAtlasWidth;
    int alphaAtlasHeight;
    float sunRayMaxDistance;
};

struct FaceOcclusion {
    float uvUAxisX;
    float uvUAxisY;
    float uvUAxisZ;
    float isTransparent;
    float uvVAxisX;
    float uvVAxisY;
    float uvVAxisZ;
    float materialIndex;
    float uvShiftX;
    float uvShiftY;
    float uvScaleX;
    float uvScaleY;
    float pixelsPerMeter;
    float baseColorAlpha;
    float baseColorR;
    float baseColorG;
    float baseColorB;
    float ior;
    float pad0;
    float pad1;
};

struct MaterialRect {
    float u0;
    float v0;
    float u1;
    float v1;
    float baseColorAlpha;
    float textureWidth;
    float textureHeight;
    int yPixelOffset;
};

const float kLightOcclusionAlphaThreshold = 0.5;
const int kMaxAlphaOcclusionSteps = 8;
const float kRayAdvanceEpsilon = 0.001;

uniform sampler2D materialAlphaAtlas;

layout(std430, binding = 0) buffer LuxelBuffer {
    Luxel luxels[];
};

layout(std430, binding = 1) readonly buffer EmissiveFaceBuffer {
    EmissiveFace emissiveFaces[];
};

layout(std430, binding = 2) readonly buffer NodeBuffer {
    BvhNode nodes[];
};

layout(std430, binding = 3) readonly buffer PrimBuffer {
    BvhPrim prims[];
};

layout(std430, binding = 4) readonly buffer ParamsBuffer {
    Params params;
};

layout(std430, binding = 5) readonly buffer LightBuffer {
    Light lights[];
};

layout(std430, binding = 6) readonly buffer ReachBuffer {
    uint reachBits[];
};

layout(std430, binding = 7) readonly buffer FaceSkyBuffer {
    int faceSky[];
};

layout(std430, binding = 8) readonly buffer FaceTransparentBuffer {
    int faceTransparent[];
};

layout(std430, binding = 12) readonly buffer FaceOcclusionBuffer {
    FaceOcclusion faceOcclusion[];
};

layout(std430, binding = 13) readonly buffer MaterialRectBuffer {
    MaterialRect materialRects[];
};

layout(std430, binding = 9) readonly buffer EmitterNodeBuffer {
    BvhNode emitterNodes[];
};

layout(std430, binding = 10) readonly buffer EmitterPrimBuffer {
    EmitterBvhPrim emitterPrims[];
};

layout(std430, binding = 11) readonly buffer EmissionGridBuffer {
    GridSample emissionGrid[];
};

layout(std430, binding = 14) readonly buffer DirectSampleBuffer {
    DirectSample directSamples[];
};

bool isSkyFace(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return false;
    }
    return faceSky[faceIndex] != 0;
}

bool isTransparentFace(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return false;
    }
    return faceTransparent[faceIndex] != 0;
}

vec3 sampleMaterialTint(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return vec3(1.0);
    }
    FaceOcclusion face = faceOcclusion[faceIndex];
    return vec3(face.baseColorR, face.baseColorG, face.baseColorB);
}

float sampleMaterialIor(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return 1.0;
    }
    return faceOcclusion[faceIndex].ior;
}

vec3 sampleFaceNormal(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return vec3(0.0, 1.0, 0.0);
    }
    FaceOcclusion face = faceOcclusion[faceIndex];
    vec3 uAxis = vec3(face.uvUAxisX, face.uvUAxisY, face.uvUAxisZ);
    vec3 vAxis = vec3(face.uvVAxisX, face.uvVAxisY, face.uvVAxisZ);
    vec3 n = cross(vAxis, uAxis);
    float len = length(n);
    return len > 1e-8 ? n / len : vec3(0.0, 1.0, 0.0);
}

bool leavesReachable(int a, int b) {
    if (params.leafCount <= 0 || params.wordsPerRow <= 0) {
        return true;
    }
    if (a < 0 || b < 0 || a >= params.leafCount || b >= params.leafCount) {
        return true;
    }
    uint word = reachBits[a * params.wordsPerRow + (b >> 5)];
    return (word & (1u << (b & 31))) != 0u;
}

float emitterLuminance(vec3 radiance) {
    return dot(radiance, vec3(0.2126, 0.7152, 0.0722));
}

bool passesCastGate(vec3 radiance) {
    return emitterLuminance(radiance) >= kMinCastLuminance;
}

float dist2PointToAabb(vec3 point, vec3 mins, vec3 maxs) {
    float dx = max(max(mins.x - point.x, 0.0), point.x - maxs.x);
    float dy = max(max(mins.y - point.y, 0.0), point.y - maxs.y);
    float dz = max(max(mins.z - point.z, 0.0), point.z - maxs.z);
    return dx * dx + dy * dy + dz * dz;
}

bool emitterPairBelowThreshold(vec3 radiance, float area, float dist2, float minDist2, float castRange) {
    if (castRange > 0.0 && dist2 > castRange * castRange) {
        return true;
    }
    float maxForm = area / (max(dist2, minDist2) * kPi);
    return emitterLuminance(radiance) * maxForm < kMinEmitterContrib;
}

float emitterRangeAttenuation(float dist, float range) {
    if (range <= 0.0) {
        return 1.0;
    }
    float t = dist / range;
    float atten = max(0.0, 1.0 - t * t);
    return atten * atten;
}

bool aabbOverlapsSphere(vec3 mins, vec3 maxs, vec3 center, float radius) {
    float dx = max(max(mins.x - center.x, 0.0), center.x - maxs.x);
    float dy = max(max(mins.y - center.y, 0.0), center.y - maxs.y);
    float dz = max(max(mins.z - center.z, 0.0), center.z - maxs.z);
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

bool rayAabb(vec3 origin, vec3 invDir, vec3 mins, vec3 maxs, float maxDistance) {
    float tmin = 0.0;
    float tmax = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        float o = axis == 0 ? origin.x : (axis == 1 ? origin.y : origin.z);
        float inv = axis == 0 ? invDir.x : (axis == 1 ? invDir.y : invDir.z);
        float b0 = axis == 0 ? mins.x : (axis == 1 ? mins.y : mins.z);
        float b1 = axis == 0 ? maxs.x : (axis == 1 ? maxs.y : maxs.z);
        float t0 = (b0 - o) * inv;
        float t1 = (b1 - o) * inv;
        if (inv < 0.0) {
            float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        tmin = max(tmin, t0);
        tmax = min(tmax, t1);
        if (tmax < tmin) {
            return false;
        }
    }
    return true;
}

bool rayTriangle(vec3 origin, vec3 direction, vec3 v0, vec3 v1, vec3 v2, float maxDistance, out float outT) {
    const float kEpsilon = 1e-6;
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 pvec = cross(direction, e2);
    float det = dot(e1, pvec);
    if (abs(det) < kEpsilon) {
        return false;
    }
    float invDet = 1.0 / det;
    vec3 tvec = origin - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }
    vec3 qvec = cross(tvec, e1);
    float v = dot(direction, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }
    float t = dot(e2, qvec) * invDet;
    if (t <= kEpsilon || t >= maxDistance) {
        return false;
    }
    outT = t;
    return true;
}

float axisScale(float scale) {
    return scale > 1e-8 ? scale : 1.0;
}

float sampleFaceOcclusionAlpha(int faceIndex, vec3 worldPos) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return 1.0;
    }
    FaceOcclusion face = faceOcclusion[faceIndex];
    int matIndex = int(face.materialIndex);
    if (matIndex < 0 || matIndex >= params.materialRectCount) {
        return face.baseColorAlpha;
    }
    MaterialRect rect = materialRects[matIndex];
    vec3 uAxis = vec3(face.uvUAxisX, face.uvUAxisY, face.uvUAxisZ);
    vec3 vAxis = vec3(face.uvVAxisX, face.uvVAxisY, face.uvVAxisZ);
    float width = max(rect.textureWidth, 1.0);
    float height = max(rect.textureHeight, 1.0);
    float ppm = max(face.pixelsPerMeter, 1.0);
    float metersU = dot(worldPos, uAxis);
    float metersV = dot(worldPos, vAxis);
    float u = (metersU * ppm * axisScale(face.uvScaleX) + face.uvShiftX) / width;
    float v = (metersV * ppm * axisScale(face.uvScaleY) + face.uvShiftY) / height;
    u = fract(u);
    v = fract(v);
    int px = clamp(int(u * width), 0, int(width) - 1);
    int py = clamp(int(v * height), 0, int(height) - 1);
    int atlasX = px;
    int atlasY = rect.yPixelOffset + py;
    float texAlpha = texelFetch(materialAlphaAtlas, ivec2(atlasX, atlasY), 0).a;
    return texAlpha * face.baseColorAlpha;
}

bool hitBlocksLight(int faceIndex, vec3 hitPoint) {
    if (!isTransparentFace(faceIndex)) {
        return true;
    }
    return sampleFaceOcclusionAlpha(faceIndex, hitPoint) >= kLightOcclusionAlphaThreshold;
}

bool raycastClosestSegment(
    vec3 origin,
    vec3 dir,
    float maxDistance,
    out int hitFaceIndex,
    out float hitDistance) {
    hitFaceIndex = -1;
    hitDistance = 0.0;
    if (params.bvhRoot < 0 || maxDistance <= 0.0) {
        return false;
    }
    vec3 invDir = vec3(
        abs(dir.x) < 1e-20 ? (dir.x >= 0.0 ? 1e20 : -1e20) : 1.0 / dir.x,
        abs(dir.y) < 1e-20 ? (dir.y >= 0.0 ? 1e20 : -1e20) : 1.0 / dir.y,
        abs(dir.z) < 1e-20 ? (dir.z >= 0.0 ? 1e20 : -1e20) : 1.0 / dir.z);

    float bestT = maxDistance;
    bool found = false;
    int hitFace = -1;
    int stack[64];
    int stackSize = 0;
    stack[stackSize++] = params.bvhRoot;

    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        BvhNode node = nodes[nodeIndex];
        vec3 mins = vec3(node.minx, node.miny, node.minz);
        vec3 maxs = vec3(node.maxx, node.maxy, node.maxz);
        if (!rayAabb(origin, invDir, mins, maxs, bestT)) {
            continue;
        }
        if (node.primCount > 0) {
            for (int i = 0; i < node.primCount; ++i) {
                BvhPrim prim = prims[node.firstPrim + i];
                float t = 0.0;
                vec3 v0 = vec3(prim.v0x, prim.v0y, prim.v0z);
                vec3 v1 = vec3(prim.v1x, prim.v1y, prim.v1z);
                vec3 v2 = vec3(prim.v2x, prim.v2y, prim.v2z);
                if (!rayTriangle(origin, dir, v0, v1, v2, bestT, t)) {
                    continue;
                }
                if (found && t >= bestT) {
                    continue;
                }
                found = true;
                bestT = t;
                hitFace = prim.faceIndex;
            }
            continue;
        }
        if (node.right >= 0 && stackSize < 64) {
            stack[stackSize++] = node.right;
        }
        if (node.left >= 0 && stackSize < 64) {
            stack[stackSize++] = node.left;
        }
    }

    if (!found) {
        return false;
    }
    hitFaceIndex = hitFace;
    hitDistance = bestT;
    return true;
}

bool raycastAny(
    vec3 origin,
    vec3 direction,
    float maxDistance,
    int ignoreFaceA,
    int ignoreFaceB,
    out int hitFaceIndex,
    inout vec3 tint) {
    hitFaceIndex = -1;
    if (params.bvhRoot < 0 || maxDistance <= 0.0) {
        return false;
    }
    float dirLen = length(direction);
    if (dirLen < 1e-8) {
        return false;
    }
    vec3 dir = direction / dirLen;
    float traveled = 0.0;

    for (int step = 0; step < kMaxAlphaOcclusionSteps; ++step) {
        if (traveled >= maxDistance) {
            return false;
        }
        vec3 rayOrigin = origin + dir * traveled;
        float remaining = maxDistance - traveled;
        int hitFace = -1;
        float hitT = 0.0;
        if (!raycastClosestSegment(rayOrigin, dir, remaining, hitFace, hitT)) {
            return false;
        }
        if (hitFace == ignoreFaceA || hitFace == ignoreFaceB) {
            traveled += hitT + kRayAdvanceEpsilon;
            continue;
        }
        vec3 hitPoint = rayOrigin + dir * hitT;
        if (hitBlocksLight(hitFace, hitPoint)) {
            hitFaceIndex = hitFace;
            return true;
        }
        tint *= sampleMaterialTint(hitFace);
        traveled += hitT + kRayAdvanceEpsilon;
    }
    return false;
}

bool raycastSunAny(
    vec3 origin,
    vec3 direction,
    float maxDistance,
    int ignoreFaceA,
    int ignoreFaceB,
    out int hitFaceIndex,
    inout vec3 tint) {
    hitFaceIndex = -1;
    if (params.bvhRoot < 0 || maxDistance <= 0.0) {
        return false;
    }
    float dirLen = length(direction);
    if (dirLen < 1e-8) {
        return false;
    }
    vec3 dir = direction / dirLen;
    vec3 rayOrigin = origin;
    float traveled = 0.0;
    bool bent = false;

    for (int step = 0; step < kMaxAlphaOcclusionSteps; ++step) {
        if (traveled >= maxDistance) {
            return false;
        }
        float remaining = maxDistance - traveled;
        int hitFace = -1;
        float hitT = 0.0;
        if (!raycastClosestSegment(rayOrigin, dir, remaining, hitFace, hitT)) {
            return false;
        }
        if (hitFace == ignoreFaceA || hitFace == ignoreFaceB) {
            rayOrigin += dir * (hitT + kRayAdvanceEpsilon);
            traveled += hitT + kRayAdvanceEpsilon;
            continue;
        }
        vec3 hitPoint = rayOrigin + dir * hitT;
        if (hitBlocksLight(hitFace, hitPoint)) {
            hitFaceIndex = hitFace;
            return true;
        }
        tint *= sampleMaterialTint(hitFace);
        float faceIor = sampleMaterialIor(hitFace);
        if (!bent && faceIor > 1.0) {
            vec3 normal = sampleFaceNormal(hitFace);
            vec3 refracted = refract(dir, normal, 1.0 / faceIor);
            if (dot(refracted, refracted) > 1e-8) {
                dir = refracted;
            }
            bent = true;
        }
        rayOrigin = hitPoint + dir * kRayAdvanceEpsilon;
        traveled += hitT + kRayAdvanceEpsilon;
    }
    return false;
}

bool segmentOccluded(vec3 from, vec3 to, int ignoreFaceA, int ignoreFaceB, inout vec3 tint) {
    vec3 delta = to - from;
    float distance = length(delta);
    if (distance < 1e-5) {
        return false;
    }
    int hitFace = -1;
    if (!raycastAny(from, delta, distance * 0.999, ignoreFaceA, ignoreFaceB, hitFace, tint)) {
        return false;
    }
    return hitFace != -1;
}

float wrapCosine(float cosine, float wrap) {
    float w = max(wrap, 0.0);
    return max(0.0, (cosine + w) / (1.0 + w));
}

vec3 sampleEmissionGridBilinear(EmissiveFace face, float u, float v) {
    if (face.gridWidth <= 0 || face.gridHeight <= 0) {
        return vec3(0.0);
    }
    float uSpan = face.uMax - face.uMin;
    float vSpan = face.vMax - face.vMin;
    if (uSpan < 1e-8 || vSpan < 1e-8) {
        return vec3(0.0);
    }
    float fu = clamp((u - face.uMin) / uSpan, 0.0, 1.0);
    float fv = clamp((v - face.vMin) / vSpan, 0.0, 1.0);
    float gx = fu * float(max(face.gridWidth - 1, 0));
    float gy = fv * float(max(face.gridHeight - 1, 0));
    int x0 = int(floor(gx));
    int y0 = int(floor(gy));
    int x1 = min(x0 + 1, face.gridWidth - 1);
    int y1 = min(y0 + 1, face.gridHeight - 1);
    float tx = gx - float(x0);
    float ty = gy - float(y0);

    int idx00 = face.gridOffset + y0 * face.gridWidth + x0;
    int idx10 = face.gridOffset + y0 * face.gridWidth + x1;
    int idx01 = face.gridOffset + y1 * face.gridWidth + x0;
    int idx11 = face.gridOffset + y1 * face.gridWidth + x1;
    if (idx11 >= emissionGrid.length()) {
        return vec3(0.0);
    }

    vec3 c00 = vec3(emissionGrid[idx00].r, emissionGrid[idx00].g, emissionGrid[idx00].b);
    vec3 c10 = vec3(emissionGrid[idx10].r, emissionGrid[idx10].g, emissionGrid[idx10].b);
    vec3 c01 = vec3(emissionGrid[idx01].r, emissionGrid[idx01].g, emissionGrid[idx01].b);
    vec3 c11 = vec3(emissionGrid[idx11].r, emissionGrid[idx11].g, emissionGrid[idx11].b);
    vec3 c0 = mix(c00, c10, tx);
    vec3 c1 = mix(c01, c11, tx);
    return mix(c0, c1, ty);
}

// Precomputed samples (built CPU-side once per face; see DirectSample above) are already
// guaranteed to be real emissive content or nothing, so they skip straight to the cast gate.
// Falls back to the blind fixed fu/fv grid for faces with no precomputed samples (solid-color
// emitters, where any point on the face is equally valid).
bool getDirectSample(
    EmissiveFace face, int sampleCount, int sx, int sy, out float u, out float v, out vec3 radiance) {
    if (face.directSampleOffset >= 0) {
        int index = face.directSampleOffset + sy * sampleCount + sx;
        if (index < 0 || index >= directSamples.length()) {
            u = 0.0;
            v = 0.0;
            radiance = vec3(0.0);
            return false;
        }
        DirectSample entry = directSamples[index];
        radiance = vec3(entry.r, entry.g, entry.b);
        u = entry.u;
        v = entry.v;
        return passesCastGate(radiance);
    }
    float fu = (float(sx) + 0.5) / float(sampleCount);
    float fv = (float(sy) + 0.5) / float(sampleCount);
    u = face.uMin + (face.uMax - face.uMin) * fu;
    v = face.vMin + (face.vMax - face.vMin) * fv;
    radiance = sampleEmissionGridBilinear(face, u, v);
    return passesCastGate(radiance);
}

vec3 planePointFromUv(EmissiveFace face, float u, float v) {
    vec3 uAxis = vec3(face.uAxisX, face.uAxisY, face.uAxisZ);
    vec3 vAxis = vec3(face.vAxisX, face.vAxisY, face.vAxisZ);
    vec3 normal = vec3(face.nx, face.ny, face.nz);
    vec3 vCrossN = cross(vAxis, normal);
    float det = dot(uAxis, vCrossN);
    if (abs(det) < 1e-12) {
        return uAxis * u + vAxis * v + normal * face.planeD;
    }
    float invDet = 1.0 / det;
    return vCrossN * (u * invDet)
        + cross(normal, uAxis) * (v * invDet)
        + cross(uAxis, vAxis) * (face.planeD * invDet);
}

void accumulateEmissiveFace(int faceIndex, vec3 luxelPos, vec3 luxelNormal, int luxelFaceIndex, int luxelLeaf,
                            float wrap, float coplanarFill, float coplanarSoft, float minDist2,
                            inout vec3 irradiance) {
    if (faceIndex < 0 || faceIndex >= params.emitterCount) {
        return;
    }
    EmissiveFace face = emissiveFaces[faceIndex];
    if (face.faceIndex == luxelFaceIndex) {
        return;
    }
    if (!leavesReachable(luxelLeaf, face.interiorLeaf)) {
        return;
    }

    vec3 peakRadiance = vec3(face.peakR, face.peakG, face.peakB);
    vec3 aabbMins = vec3(face.aabbMinX, face.aabbMinY, face.aabbMinZ);
    vec3 aabbMaxs = vec3(face.aabbMaxX, face.aabbMaxY, face.aabbMaxZ);
    float dist2Raw = dist2PointToAabb(luxelPos, aabbMins, aabbMaxs);
    if (emitterPairBelowThreshold(peakRadiance, face.area, dist2Raw, minDist2, face.castRange)) {
        return;
    }

    int sampleCount = max(params.emitterDirectSamples, 1);
    int validSamples = 0;
    for (int sy = 0; sy < sampleCount; ++sy) {
        for (int sx = 0; sx < sampleCount; ++sx) {
            float u, v;
            vec3 radiance;
            if (getDirectSample(face, sampleCount, sx, sy, u, v, radiance)) {
                validSamples += 1;
            }
        }
    }
    if (validSamples <= 0) {
        return;
    }

    float sampleArea = face.area / float(validSamples);
    vec3 faceNormal = vec3(face.nx, face.ny, face.nz);
    const float kCoplanarAlignMin = 0.85;

    for (int sy = 0; sy < sampleCount; ++sy) {
        for (int sx = 0; sx < sampleCount; ++sx) {
            float u, v;
            vec3 radiance;
            if (!getDirectSample(face, sampleCount, sx, sy, u, v, radiance)) {
                continue;
            }

            vec3 samplePos = planePointFromUv(face, u, v) + faceNormal * kEmitterNormalOffset;
            vec3 delta = samplePos - luxelPos;
            float sampleDist2Raw = dot(delta, delta);
            if (sampleDist2Raw < 1e-6) {
                continue;
            }
            float sampleDist = sqrt(sampleDist2Raw);
            if (face.castRange > 0.0 && sampleDist > face.castRange) {
                continue;
            }
            vec3 toLight = delta / sampleDist;
            float dist2 = max(sampleDist2Raw, minDist2);
            float nDotL = wrapCosine(dot(luxelNormal, toLight), wrap);
            float nDotV = wrapCosine(-dot(faceNormal, toLight), wrap);
            bool formOk = nDotL > 0.0 && nDotV > 0.0;
            float align = 0.0;
            bool fillOk = false;
            if (coplanarFill > 0.0) {
                align = dot(luxelNormal, faceNormal);
                fillOk = align > kCoplanarAlignMin;
            }
            if (!formOk && !fillOk) {
                continue;
            }
            vec3 segmentTint = vec3(1.0);
            if (segmentOccluded(luxelPos, samplePos, luxelFaceIndex, face.faceIndex, segmentTint)) {
                continue;
            }

            if (formOk) {
                float form = nDotL * nDotV * sampleArea / (dist2 * kPi);
                float atten = emitterRangeAttenuation(sampleDist, face.castRange);
                if (!isnan(form) && !isinf(form)) {
                    irradiance += radiance * form * atten * segmentTint;
                }
            }
            if (fillOk) {
                float planeSep = abs(dot(delta, luxelNormal));
                float lateral2 = max(0.0, sampleDist2Raw - planeSep * planeSep);
                float weight = align * exp(-planeSep / coplanarSoft) / (lateral2 + minDist2);
                float fill = sampleArea * coplanarFill * weight / (4.0 * kPi);
                float atten = emitterRangeAttenuation(sampleDist, face.castRange);
                if (!isnan(fill) && !isinf(fill)) {
                    irradiance += radiance * fill * atten * segmentTint;
                }
            }
        }
    }
}

void traverseEmitterBvh(vec3 luxelPos, vec3 luxelNormal, int luxelFaceIndex, int luxelLeaf,
                        float wrap, float coplanarFill, float coplanarSoft, float minDist2,
                        inout vec3 irradiance) {
    if (params.emitterBvhRoot < 0 || params.emitterQueryRadius <= 0.0) {
        return;
    }
    int stack[64];
    int stackSize = 0;
    stack[stackSize++] = params.emitterBvhRoot;
    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        BvhNode node = emitterNodes[nodeIndex];
        vec3 mins = vec3(node.minx, node.miny, node.minz);
        vec3 maxs = vec3(node.maxx, node.maxy, node.maxz);
        if (!aabbOverlapsSphere(mins, maxs, luxelPos, params.emitterQueryRadius)) {
            continue;
        }
        if (node.primCount > 0) {
            for (int i = 0; i < node.primCount; ++i) {
                EmitterBvhPrim prim = emitterPrims[node.firstPrim + i];
                vec3 primMins = vec3(prim.minx, prim.miny, prim.minz);
                vec3 primMaxs = vec3(prim.maxx, prim.maxy, prim.maxz);
                if (!aabbOverlapsSphere(primMins, primMaxs, luxelPos, params.emitterQueryRadius)) {
                    continue;
                }
                accumulateEmissiveFace(prim.emitterIndex, luxelPos, luxelNormal, luxelFaceIndex, luxelLeaf,
                                       wrap, coplanarFill, coplanarSoft, minDist2, irradiance);
            }
            continue;
        }
        if (node.right >= 0 && stackSize < 64) {
            stack[stackSize++] = node.right;
        }
        if (node.left >= 0 && stackSize < 64) {
            stack[stackSize++] = node.left;
        }
    }
}

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return x < edge0 ? 0.0 : 1.0;
    }
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void buildSunBasis(vec3 toLight, out vec3 tangentOut, out vec3 bitangentOut) {
    vec3 helper = abs(toLight.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangentOut = normalize(cross(helper, toLight));
    bitangentOut = normalize(cross(toLight, tangentOut));
}

vec3 sampleSunRayDirection(
    vec3 toLight,
    vec3 tangent,
    vec3 bitangent,
    float angularSpreadRad,
    int rayIndex,
    int rayCount) {
    if (rayCount <= 1 || angularSpreadRad <= 0.0) {
        return toLight;
    }
    int strataN = max(1, int(floor(sqrt(float(rayCount)))));
    int strataM = max(1, (rayCount + strataN - 1) / strataN);
    int sy = rayIndex / strataN;
    int sx = rayIndex % strataN;
    float fu = (float(sx) + 0.5) / float(strataN);
    float fv = (float(sy) + 0.5) / float(strataM);
    float r = sqrt(fu);
    float theta = fv * 6.28318530718;
    float dx = r * cos(theta);
    float dy = r * sin(theta);
    float spread = tan(angularSpreadRad);
    return normalize(toLight + tangent * (dx * spread) + bitangent * (dy * spread));
}

float sunSkyVisibility(
    vec3 luxelPos,
    int luxelFaceIndex,
    vec3 toLight,
    vec3 tangent,
    vec3 bitangent,
    out vec3 tintOut) {
    tintOut = vec3(1.0);
    if (params.sunRayCount <= 1 || params.sunAngularSpread <= 0.0) {
        int hitFace = -1;
        vec3 rayTint = vec3(1.0);
        if (!raycastSunAny(luxelPos, toLight, params.sunRayMaxDistance, luxelFaceIndex, -1, hitFace, rayTint)
            || !isSkyFace(hitFace)) {
            return 0.0;
        }
        tintOut = rayTint;
        return 1.0;
    }

    float hits = 0.0;
    vec3 tintSum = vec3(0.0);
    for (int ray = 0; ray < params.sunRayCount; ++ray) {
        vec3 rayDir = sampleSunRayDirection(
            toLight,
            tangent,
            bitangent,
            params.sunAngularSpread,
            ray,
            params.sunRayCount);
        int hitFace = -1;
        vec3 rayTint = vec3(1.0);
        if (raycastSunAny(luxelPos, rayDir, params.sunRayMaxDistance, luxelFaceIndex, -1, hitFace, rayTint)
            && isSkyFace(hitFace)) {
            hits += 1.0;
            tintSum += rayTint;
        }
    }
    float visibility = hits / float(params.sunRayCount);
    if (hits > 0.0) {
        tintOut = tintSum / hits;
    }
    if (visibility <= params.sunLeakThreshold) {
        return 0.0;
    }
    float leakThreshold = clamp(params.sunLeakThreshold, 0.0, 0.999);
    return (visibility - leakThreshold) / (1.0 - leakThreshold);
}

void main() {
    uint localId = gl_GlobalInvocationID.x;
    if (localId >= uint(params.luxelBatch)) {
        return;
    }
    uint id = uint(params.luxelOffset) + localId;
    if (id >= uint(params.luxelCount)) {
        return;
    }

    Luxel luxel = luxels[id];
    if (luxel.covered != 0) {
        return;
    }

    vec3 luxelPos = vec3(luxel.px, luxel.py, luxel.pz);
    vec3 luxelNormal = vec3(luxel.nx, luxel.ny, luxel.nz);
    vec3 irradiance = vec3(luxel.ir, luxel.ig, luxel.ib);
    vec3 sunIrradiance = vec3(luxel.sunIr, luxel.sunIg, luxel.sunIb);
    float wrap = params.directWrap;
    float coplanarFill = max(params.coplanarFill, 0.0);
    float coplanarSoft = max(params.coplanarSoft, 1e-4);
    float minDist2 = max(params.minDist2, 1e-6);

    if (params.emitterBvhRoot >= 0) {
        traverseEmitterBvh(luxelPos, luxelNormal, luxel.faceIndex, luxel.leafIndex,
                           wrap, coplanarFill, coplanarSoft, minDist2, irradiance);
    }

    int lightBegin = params.lightOffset;
    int lightEnd = min(params.lightOffset + params.lightBatch, params.lightCount);
    for (int li = lightBegin; li < lightEnd; ++li) {
        Light light = lights[li];
        if (!leavesReachable(luxel.leafIndex, light.leafIndex)) {
            continue;
        }

        vec3 intensity = vec3(light.cr, light.cg, light.cb) * light.intensity;

        if (light.kind == kLightKindSun) {
            vec3 forward = vec3(light.dx, light.dy, light.dz);
            float forwardLen = length(forward);
            if (forwardLen < 1e-6) {
                continue;
            }
            vec3 toLight = -forward / forwardLen;
            float nDotL = wrapCosine(dot(luxelNormal, toLight), wrap);
            if (nDotL <= 0.0) {
                continue;
            }
            vec3 tangent;
            vec3 bitangent;
            buildSunBasis(toLight, tangent, bitangent);
            vec3 sunTint;
            float visibility = sunSkyVisibility(luxelPos, luxel.faceIndex, toLight, tangent, bitangent, sunTint);
            if (visibility <= 0.0) {
                continue;
            }
            vec3 contrib = intensity * (nDotL * visibility) * sunTint;
            if (!isnan(contrib.x) && !isnan(contrib.y) && !isnan(contrib.z)
                && !isinf(contrib.x) && !isinf(contrib.y) && !isinf(contrib.z)) {
                irradiance += contrib;
                sunIrradiance += contrib;
            }
            continue;
        }

        vec3 lightPos = vec3(light.px, light.py, light.pz);
        vec3 delta = lightPos - luxelPos;
        float dist2Raw = dot(delta, delta);
        if (dist2Raw < 1e-6) {
            continue;
        }
        float dist = sqrt(dist2Raw);
        float range = max(light.range, 1e-4);
        if (dist > range) {
            continue;
        }
        vec3 toLight = delta / dist;
        float dist2 = max(dist2Raw, minDist2);
        float nDotL = wrapCosine(dot(luxelNormal, toLight), wrap);
        if (nDotL <= 0.0) {
            continue;
        }

        float spot = 1.0;
        if (light.kind == kLightKindSpot) {
            vec3 forward = vec3(light.dx, light.dy, light.dz);
            float forwardLen = length(forward);
            if (forwardLen < 1e-6) {
                continue;
            }
            forward /= forwardLen;
            float cosTheta = dot(-toLight, forward);
            float cosOuter = cos(light.coneAngle);
            float cosInner = cos(light.coneAngle * 0.8);
            spot = smoothstep(cosOuter, cosInner, cosTheta);
            if (spot <= 0.0) {
                continue;
            }
        }

        vec3 pointTint = vec3(1.0);
        if (segmentOccluded(luxelPos, lightPos, luxel.faceIndex, -1, pointTint)) {
            continue;
        }

        float t = dist / range;
        float atten = max(0.0, 1.0 - t * t);
        atten *= atten;

        vec3 contrib = intensity * (nDotL * atten * spot / dist2) * pointTint;
        if (!isnan(contrib.x) && !isnan(contrib.y) && !isnan(contrib.z)
            && !isinf(contrib.x) && !isinf(contrib.y) && !isinf(contrib.z)) {
            irradiance += contrib;
        }
    }

    if (isnan(irradiance.x) || isnan(irradiance.y) || isnan(irradiance.z)
        || isinf(irradiance.x) || isinf(irradiance.y) || isinf(irradiance.z)) {
        irradiance = vec3(luxel.ir, luxel.ig, luxel.ib);
        sunIrradiance = vec3(luxel.sunIr, luxel.sunIg, luxel.sunIb);
    }
    luxels[id].ir = irradiance.x;
    luxels[id].ig = irradiance.y;
    luxels[id].ib = irradiance.z;
    luxels[id].sunIr = sunIrradiance.x;
    luxels[id].sunIg = sunIrradiance.y;
    luxels[id].sunIb = sunIrradiance.z;
}
