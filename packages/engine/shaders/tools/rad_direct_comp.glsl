#version 430

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const float kPi = 3.14159265358979323846;
const float kMinEmitterContrib = 1e-5;
const int kLightKindPoint = 0;
const int kLightKindSpot = 1;
const int kLightKindSun = 2;
const float kSunRayDistance = 1000.0;

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
};

struct Emitter {
    float px;
    float py;
    float pz;
    float area;
    float nx;
    float ny;
    float nz;
    int faceIndex;
    float rr;
    float rg;
    float rb;
    int leafIndex;
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
};

layout(std430, binding = 0) buffer LuxelBuffer {
    Luxel luxels[];
};

layout(std430, binding = 1) readonly buffer EmitterBuffer {
    Emitter emitters[];
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

layout(std430, binding = 9) readonly buffer EmitterNodeBuffer {
    BvhNode emitterNodes[];
};

layout(std430, binding = 10) readonly buffer EmitterPrimBuffer {
    EmitterBvhPrim emitterPrims[];
};

bool isSkyFace(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= faceSky.length()) {
        return false;
    }
    return faceSky[faceIndex] != 0;
}

bool isTransparentFace(int faceIndex) {
    if (faceIndex < 0 || faceIndex >= faceTransparent.length()) {
        return false;
    }
    return faceTransparent[faceIndex] != 0;
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

bool raycastAny(
    vec3 origin,
    vec3 direction,
    float maxDistance,
    int ignoreFaceA,
    int ignoreFaceB,
    out int hitFaceIndex) {
    if (params.bvhRoot < 0 || maxDistance <= 0.0) {
        return false;
    }
    float dirLen = length(direction);
    if (dirLen < 1e-8) {
        return false;
    }
    vec3 dir = direction / dirLen;
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
                if (prim.faceIndex == ignoreFaceA || prim.faceIndex == ignoreFaceB) {
                    continue;
                }
                if (isTransparentFace(prim.faceIndex)) {
                    continue;
                }
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

    hitFaceIndex = hitFace;
    return found;
}

bool segmentOccluded(vec3 from, vec3 to, int ignoreFaceA, int ignoreFaceB) {
    vec3 delta = to - from;
    float distance = length(delta);
    if (distance < 1e-5) {
        return false;
    }
    int hitFace = -1;
    if (!raycastAny(from, delta, distance * 0.999, ignoreFaceA, ignoreFaceB, hitFace)) {
        return false;
    }
    return hitFace != -1;
}

float wrapCosine(float cosine, float wrap) {
    float w = max(wrap, 0.0);
    return max(0.0, (cosine + w) / (1.0 + w));
}

void accumulateEmitter(int emitterIndex, vec3 luxelPos, vec3 luxelNormal, int luxelFaceIndex, int luxelLeaf,
                       float wrap, float coplanarFill, float coplanarSoft, float minDist2,
                       inout vec3 irradiance) {
    if (emitterIndex < 0 || emitterIndex >= params.emitterCount) {
        return;
    }
    Emitter emitter = emitters[emitterIndex];
    if (emitter.faceIndex == luxelFaceIndex) {
        return;
    }
    if (!leavesReachable(luxelLeaf, emitter.leafIndex)) {
        return;
    }
    vec3 emitterPos = vec3(emitter.px, emitter.py, emitter.pz);
    vec3 emitterNormal = vec3(emitter.nx, emitter.ny, emitter.nz);
    vec3 delta = emitterPos - luxelPos;
    float dist2Raw = dot(delta, delta);
    if (dist2Raw < 1e-6) {
        return;
    }
    float dist = sqrt(dist2Raw);
    vec3 toLight = delta / dist;
    float dist2 = max(dist2Raw, minDist2);
    float nDotL = wrapCosine(dot(luxelNormal, toLight), wrap);
    float nDotV = wrapCosine(-dot(emitterNormal, toLight), wrap);
    bool formOk = nDotL > 0.0 && nDotV > 0.0;
    float align = 0.0;
    bool fillOk = false;
    const float kCoplanarAlignMin = 0.85;
    if (coplanarFill > 0.0) {
        align = dot(luxelNormal, emitterNormal);
        fillOk = align > kCoplanarAlignMin;
    }
    if (!formOk && !fillOk) {
        return;
    }
    vec3 radiance = vec3(emitter.rr, emitter.rg, emitter.rb);
    float lum = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
    float maxDist2 = lum * emitter.area / (kMinEmitterContrib * kPi);
    if (dist2Raw > maxDist2) {
        return;
    }
    float maxForm = emitter.area / (max(dist2, minDist2) * kPi);
    if (lum * maxForm < kMinEmitterContrib) {
        return;
    }
    if (segmentOccluded(luxelPos, emitterPos, luxelFaceIndex, emitter.faceIndex)) {
        return;
    }

    if (formOk) {
        float form = nDotL * nDotV * emitter.area / (dist2 * kPi);
        if (!isnan(form) && !isinf(form)) {
            irradiance += radiance * form;
        }
    }

    if (fillOk) {
        float planeSep = abs(dot(delta, luxelNormal));
        float lateral2 = max(0.0, dist2Raw - planeSep * planeSep);
        float weight = align * exp(-planeSep / coplanarSoft) / (lateral2 + minDist2);
        float fill = emitter.area * coplanarFill * weight / (4.0 * kPi);
        if (!isnan(fill) && !isinf(fill)) {
            irradiance += radiance * fill;
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
                accumulateEmitter(prim.emitterIndex, luxelPos, luxelNormal, luxelFaceIndex, luxelLeaf,
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
            int hitFace = -1;
            if (!raycastAny(luxelPos, toLight, kSunRayDistance, luxel.faceIndex, -1, hitFace)
                || !isSkyFace(hitFace)) {
                continue;
            }
            vec3 contrib = intensity * nDotL;
            if (!isnan(contrib.x) && !isnan(contrib.y) && !isnan(contrib.z)
                && !isinf(contrib.x) && !isinf(contrib.y) && !isinf(contrib.z)) {
                irradiance += contrib;
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

        if (segmentOccluded(luxelPos, lightPos, luxel.faceIndex, -1)) {
            continue;
        }

        float t = dist / range;
        float atten = max(0.0, 1.0 - t * t);
        atten *= atten;

        vec3 contrib = intensity * (nDotL * atten * spot / dist2);
        if (!isnan(contrib.x) && !isnan(contrib.y) && !isnan(contrib.z)
            && !isinf(contrib.x) && !isinf(contrib.y) && !isinf(contrib.z)) {
            irradiance += contrib;
        }
    }

    if (isnan(irradiance.x) || isnan(irradiance.y) || isnan(irradiance.z)
        || isinf(irradiance.x) || isinf(irradiance.y) || isinf(irradiance.z)) {
        irradiance = vec3(luxel.ir, luxel.ig, luxel.ib);
    }
    luxels[id].ir = irradiance.x;
    luxels[id].ig = irradiance.y;
    luxels[id].ib = irradiance.z;
}
