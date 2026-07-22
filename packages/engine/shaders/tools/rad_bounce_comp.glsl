#version 430

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct BounceLuxel {
    float px;
    float py;
    float pz;
    int covered;
    float nx;
    float ny;
    float nz;
    int faceIndex;
    int localX;
    int localY;
    int pad0;
    int pad1;
};

struct Rgb {
    float r;
    float g;
    float b;
    float pad;
};

struct FaceGrid {
    int luxelBase;
    int luxelWidth;
    int luxelHeight;
    int valid;
    float ux;
    float uy;
    float uz;
    float uMin;
    float vx;
    float vy;
    float vz;
    float vMin;
    float uMax;
    float vMax;
    float pad0;
    float pad1;
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

struct Params {
    int luxelCount;
    int faceCount;
    int sampleCount;
    int bvhRoot;
    int luxelOffset;
    int luxelBatch;
    uint seed;
    float rayMaxDistance;
    float ambientR;
    float ambientG;
    float ambientB;
    float pad;
};

layout(std430, binding = 0) readonly buffer LuxelBuffer {
    BounceLuxel luxels[];
};

layout(std430, binding = 1) writeonly buffer GatherBuffer {
    Rgb gathered[];
};

layout(std430, binding = 2) readonly buffer ShootBuffer {
    Rgb shoot[];
};

layout(std430, binding = 3) readonly buffer FaceGridBuffer {
    FaceGrid faceGrids[];
};

layout(std430, binding = 4) readonly buffer NodeBuffer {
    BvhNode nodes[];
};

layout(std430, binding = 5) readonly buffer PrimBuffer {
    BvhPrim prims[];
};

layout(std430, binding = 6) readonly buffer ParamsBuffer {
    Params params;
};

uint hashU(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float hashToUnit(uint x) {
    return float(hashU(x) & 0x00ffffffu) / float(0x01000000u);
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

bool raycastClosest(
    vec3 origin,
    vec3 direction,
    float maxDistance,
    int ignoreFace,
    out int hitFaceIndex,
    out vec3 hitPoint) {
    hitFaceIndex = -1;
    hitPoint = origin;
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
                if (prim.faceIndex == ignoreFace) {
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

    if (!found) {
        return false;
    }
    hitFaceIndex = hitFace;
    hitPoint = origin + dir * bestT;
    return true;
}

vec3 cosineHemisphere(vec3 normal, float u1, float u2) {
    float ax = abs(normal.x);
    float ay = abs(normal.y);
    float az = abs(normal.z);
    vec3 helper = ax < ay ? (ax < az ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0))
                          : (ay < az ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
    vec3 tangent = normalize(cross(normal, helper));
    vec3 bitangent = cross(normal, tangent);
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1));
    return normalize(tangent * x + bitangent * y + normal * z);
}

vec3 sampleFaceRadiance(int faceIndex, vec3 point, vec3 fallback) {
    if (faceIndex < 0 || faceIndex >= params.faceCount) {
        return fallback;
    }
    FaceGrid grid = faceGrids[faceIndex];
    if (grid.valid == 0 || grid.luxelWidth <= 0 || grid.luxelHeight <= 0) {
        return fallback;
    }
    float uSpan = grid.uMax - grid.uMin;
    float vSpan = grid.vMax - grid.vMin;
    if (uSpan < 1e-8 || vSpan < 1e-8) {
        return fallback;
    }
    vec3 uAxis = vec3(grid.ux, grid.uy, grid.uz);
    vec3 vAxis = vec3(grid.vx, grid.vy, grid.vz);
    float u = dot(point, uAxis);
    float v = dot(point, vAxis);
    float fu = (u - grid.uMin) / uSpan;
    float fv = (v - grid.vMin) / vSpan;
    float fx = fu * float(grid.luxelWidth) - 0.5;
    float fy = fv * float(grid.luxelHeight) - 0.5;
    int x0 = int(floor(fx));
    int y0 = int(floor(fy));
    float tx = fx - float(x0);
    float ty = fy - float(y0);

    vec3 c00 = fallback;
    vec3 c10 = fallback;
    vec3 c01 = fallback;
    vec3 c11 = fallback;
    for (int si = 0; si < 4; ++si) {
        int sx = x0 + (si & 1);
        int sy = y0 + ((si >> 1) & 1);
        sx = clamp(sx, 0, grid.luxelWidth - 1);
        sy = clamp(sy, 0, grid.luxelHeight - 1);
        int index = grid.luxelBase + sy * grid.luxelWidth + sx;
        Rgb rgb = shoot[index];
        vec3 c = vec3(rgb.r, rgb.g, rgb.b);
        if (si == 0) {
            c00 = c;
        } else if (si == 1) {
            c10 = c;
        } else if (si == 2) {
            c01 = c;
        } else {
            c11 = c;
        }
    }
    vec3 c0 = mix(c00, c10, tx);
    vec3 c1 = mix(c01, c11, tx);
    return mix(c0, c1, ty);
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

    BounceLuxel luxel = luxels[id];
    if (luxel.covered != 0) {
        gathered[id] = Rgb(0.0, 0.0, 0.0, 0.0);
        return;
    }

    int sampleCount = max(params.sampleCount, 1);
    int strataN = max(int(floor(sqrt(float(sampleCount)))), 1);
    int strataM = max((sampleCount + strataN - 1) / strataN, 1);
    vec3 normal = vec3(luxel.nx, luxel.ny, luxel.nz);
    vec3 origin = vec3(luxel.px, luxel.py, luxel.pz);
    vec3 ambient = vec3(params.ambientR, params.ambientG, params.ambientB) * 0.25;
    vec3 sum = vec3(0.0);
    int fired = 0;
    uint baseSeed = params.seed
        ^ hashU(uint(luxel.faceIndex))
        ^ hashU(uint(luxel.localX) * 0x9e3779b9u + uint(luxel.localY));

    for (int sy = 0; sy < strataM && fired < sampleCount; ++sy) {
        for (int sx = 0; sx < strataN && fired < sampleCount; ++sx) {
            uint s = baseSeed + uint(fired) * 0x85ebca6bu;
            float u1 = (float(sx) + hashToUnit(s)) / float(strataN);
            float u2 = (float(sy) + hashToUnit(s ^ 0x27d4eb2du)) / float(strataM);
            vec3 dir = cosineHemisphere(normal, u1, u2);
            int hitFace = -1;
            vec3 hitPoint = origin;
            ++fired;
            if (!raycastClosest(origin, dir, params.rayMaxDistance, luxel.faceIndex, hitFace, hitPoint)) {
                continue;
            }
            sum += sampleFaceRadiance(hitFace, hitPoint, ambient);
        }
    }

    sum /= float(sampleCount);
    if (isnan(sum.x) || isnan(sum.y) || isnan(sum.z) || isinf(sum.x) || isinf(sum.y) || isinf(sum.z)) {
        sum = vec3(0.0);
    }
    gathered[id] = Rgb(sum.x, sum.y, sum.z, 0.0);
}
