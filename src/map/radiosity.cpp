#include "map/radiosity.hpp"

#include "map/bsp.hpp"
#include "map/bsp_ray.hpp"
#include "map/quad_bvh.hpp"
#include "map/radiosity_gpu.hpp"
#include "map/uv_math.hpp"

#include <raylib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace slopengine {

namespace {

struct Color3 {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    Color3& operator+=(const Color3& other) {
        r += other.r;
        g += other.g;
        b += other.b;
        return *this;
    }

    Color3 operator*(float s) const { return {r * s, g * s, b * s}; }

    Color3 operator*(const Color3& other) const { return {r * other.r, g * other.g, b * other.b}; }
};

Color3 operator+(Color3 a, const Color3& b) {
    a += b;
    return a;
}

Color3 operator-(Color3 a, const Color3& b) {
    return {a.r - b.r, a.g - b.g, a.b - b.b};
}

Color3 max0(Color3 c) {
    return {std::max(0.0f, c.r), std::max(0.0f, c.g), std::max(0.0f, c.b)};
}

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 add3(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 sub3(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 scale3(Vector3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vector3 normalize3(Vector3 v) {
    const float len = std::sqrt(dot3(v, v));
    if (len < 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return scale3(v, 1.0f / len);
}

Vector3 cross3(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float luxelFaceParam(int index, int luxelCount) {
    if (luxelCount <= 1) {
        return 0.5f;
    }
    return static_cast<float>(index) / static_cast<float>(luxelCount - 1);
}

Vector3 planePointFromUv(
    Vector3 uAxis,
    Vector3 vAxis,
    Vector3 normal,
    float u,
    float v,
    float planeD) {
    const Vector3 vCrossN = cross3(vAxis, normal);
    const float det = dot3(uAxis, vCrossN);
    if (std::fabs(det) < 1e-12f) {
        return add3(add3(scale3(uAxis, u), scale3(vAxis, v)), scale3(normal, planeD));
    }
    const float invDet = 1.0f / det;
    return add3(
        add3(scale3(vCrossN, u * invDet), scale3(cross3(normal, uAxis), v * invDet)),
        scale3(cross3(uAxis, vAxis), planeD * invDet));
}

Color3 colorFromRaylib(Color c) {
    return {
        static_cast<float>(c.r) / 255.0f,
        static_cast<float>(c.g) / 255.0f,
        static_cast<float>(c.b) / 255.0f,
    };
}

float luminance(const Color3& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

Color3 sampleImageUv(const Image& image, float u, float v) {
    if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
        return {1.0f, 1.0f, 1.0f};
    }
    u = u - std::floor(u);
    v = v - std::floor(v);
    const int x = std::clamp(static_cast<int>(u * static_cast<float>(image.width)), 0, image.width - 1);
    const int y = std::clamp(static_cast<int>(v * static_cast<float>(image.height)), 0, image.height - 1);
    return colorFromRaylib(GetImageColor(image, x, y));
}

struct FaceBasis {
    Vector3 normal{};
    Vector3 uAxis{};
    Vector3 vAxis{};
    float planeD = 0.0f;
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
};

FaceBasis makeFaceBasis(const LightmapFace& face) {
    FaceBasis basis;
    basis.normal = face.normal;
    faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, basis.uAxis, basis.vAxis);
    basis.planeD = dot3(face.vertices[0], face.normal);
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
        const float u = dot3(face.vertices[i], basis.uAxis);
        const float v = dot3(face.vertices[i], basis.vAxis);
        if (i == 0) {
            basis.uMin = basis.uMax = u;
            basis.vMin = basis.vMax = v;
        } else {
            basis.uMin = std::min(basis.uMin, u);
            basis.uMax = std::max(basis.uMax, u);
            basis.vMin = std::min(basis.vMin, v);
            basis.vMax = std::max(basis.vMax, v);
        }
    }
    return basis;
}

Vector3 luxelWorldPos(const FaceBasis& basis, const LightmapChart& chart, int x, int y) {
    const float fu = luxelFaceParam(x, chart.luxelWidth);
    const float fv = luxelFaceParam(y, chart.luxelHeight);
    const float u = basis.uMin + (basis.uMax - basis.uMin) * fu;
    const float v = basis.vMin + (basis.vMax - basis.vMin) * fv;
    return planePointFromUv(basis.uAxis, basis.vAxis, basis.normal, u, v, basis.planeD);
}

struct EmitterPatch {
    Vector3 position{};
    Vector3 normal{};
    Color3 radiance{};
    float area = 0.0f;
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
};

struct FaceLuxelGrid {
    bool valid = false;
    std::size_t luxelBase = 0;
    int luxelWidth = 0;
    int luxelHeight = 0;
};

struct LeafReachability {
    bool enabled = false;
    int leafCount = 0;
    int wordsPerRow = 0;
    std::vector<std::uint32_t> bits;

    bool canSee(std::int32_t a, std::int32_t b) const {
        if (!enabled) {
            return true;
        }
        if (a < 0 || b < 0 || a >= leafCount || b >= leafCount) {
            return true;
        }
        const std::uint32_t word =
            bits[static_cast<std::size_t>(a * wordsPerRow + (b >> 5))];
        return (word & (1u << (b & 31))) != 0u;
    }
};

LeafReachability buildOpenLeafReachability(const BspTree& tree) {
    LeafReachability reach;
    const int n = static_cast<int>(tree.leaves.size());
    if (n <= 0) {
        return reach;
    }
    reach.enabled = true;
    reach.leafCount = n;
    reach.wordsPerRow = (n + 31) / 32;
    reach.bits.assign(static_cast<std::size_t>(n * reach.wordsPerRow), 0u);

    auto setBit = [&](int from, int to) {
        reach.bits[static_cast<std::size_t>(from * reach.wordsPerRow + (to >> 5))] |=
            (1u << (to & 31));
    };

    for (int start = 0; start < n; ++start) {
        if (!leafIsOpen(tree.leaves[static_cast<std::size_t>(start)].contents)) {
            continue;
        }
        std::vector<char> visited(static_cast<std::size_t>(n), 0);
        std::vector<int> queue;
        queue.push_back(start);
        visited[static_cast<std::size_t>(start)] = 1;
        for (std::size_t qi = 0; qi < queue.size(); ++qi) {
            const int cur = queue[qi];
            setBit(start, cur);
            for (std::int32_t nb : tree.leaves[static_cast<std::size_t>(cur)].neighbors) {
                if (nb < 0 || nb >= n || visited[static_cast<std::size_t>(nb)] != 0) {
                    continue;
                }
                if (!leafIsOpen(tree.leaves[static_cast<std::size_t>(nb)].contents)) {
                    continue;
                }
                visited[static_cast<std::size_t>(nb)] = 1;
                queue.push_back(nb);
            }
        }
    }
    return reach;
}

bool pointInFacePolygon(const LightmapFace& face, const FaceBasis& basis, float u, float v) {
    if (face.vertices.size() < 3) {
        return false;
    }
    bool inside = false;
    const std::size_t count = face.vertices.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const float ui = dot3(face.vertices[i], basis.uAxis);
        const float vi = dot3(face.vertices[i], basis.vAxis);
        const float uj = dot3(face.vertices[j], basis.uAxis);
        const float vj = dot3(face.vertices[j], basis.vAxis);
        if ((vi > v) != (vj > v)
            && u < (uj - ui) * (v - vi) / (vj - vi) + ui) {
            inside = !inside;
        }
    }
    return inside;
}

std::uint32_t hashLuxelSeed(std::int32_t faceIndex, int x, int y) {
    std::uint32_t h = 2166136261u;
    h ^= static_cast<std::uint32_t>(faceIndex);
    h *= 16777619u;
    h ^= static_cast<std::uint32_t>(x);
    h *= 16777619u;
    h ^= static_cast<std::uint32_t>(y);
    h *= 16777619u;
    return h;
}

Color3 emissionAt(
    const LightmapFace& face,
    const MaterialBakeInfo& material,
    Vector3 worldPos) {
    Color3 emitColor{
        static_cast<float>(material.asset.emissionColor.r) / 255.0f,
        static_cast<float>(material.asset.emissionColor.g) / 255.0f,
        static_cast<float>(material.asset.emissionColor.b) / 255.0f,
    };
    emitColor = emitColor * material.asset.emissionPower;

    if (material.hasEmissionImage) {
        Vector3 uAxis{};
        Vector3 vAxis{};
        faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
        MaterialUvInfo uvInfo{};
        uvInfo.pixelsPerMeter = material.asset.pixelsPerMeter;
        uvInfo.textureWidth = static_cast<float>(material.emissionImage.width);
        uvInfo.textureHeight = static_cast<float>(material.emissionImage.height);
        const Vector2 uv =
            worldPlanarUv(worldPos, uAxis, vAxis, face.uvShiftPixels, face.uvScale, uvInfo);
        const Color3 texel = sampleImageUv(material.emissionImage, uv.x, uv.y);
        if (luminance(texel) < 1.0f / 255.0f) {
            return {};
        }
        if (material.asset.emissionPower <= 0.0f && luminance(emitColor) <= 0.0f) {
            return texel;
        }
        return emitColor * texel;
    }

    if (material.asset.emissionPower <= 0.0f || luminance(emitColor) <= 0.0f) {
        return {};
    }
    return emitColor;
}

Color3 albedoAt(
    const LightmapFace& face,
    const MaterialBakeInfo& material,
    Vector3 worldPos) {
    Color3 base{
        static_cast<float>(material.asset.baseColor.r) / 255.0f,
        static_cast<float>(material.asset.baseColor.g) / 255.0f,
        static_cast<float>(material.asset.baseColor.b) / 255.0f,
    };
    if (!material.hasAlbedoImage) {
        return base;
    }
    Vector3 uAxis{};
    Vector3 vAxis{};
    faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
    MaterialUvInfo uvInfo{};
    uvInfo.pixelsPerMeter = material.asset.pixelsPerMeter;
    uvInfo.textureWidth = static_cast<float>(material.albedoImage.width);
    uvInfo.textureHeight = static_cast<float>(material.albedoImage.height);
    const Vector2 uv =
        worldPlanarUv(worldPos, uAxis, vAxis, face.uvShiftPixels, face.uvScale, uvInfo);
    return base * sampleImageUv(material.albedoImage, uv.x, uv.y);
}

Vector3 cosineHemisphere(Vector3 normal, float u1, float u2) {
    float ax = std::fabs(normal.x);
    float ay = std::fabs(normal.y);
    float az = std::fabs(normal.z);
    Vector3 helper = ax < ay ? (ax < az ? Vector3{1, 0, 0} : Vector3{0, 0, 1})
                             : (ay < az ? Vector3{0, 1, 0} : Vector3{0, 0, 1});
    Vector3 tangent = normalize3(Vector3{
        normal.y * helper.z - normal.z * helper.y,
        normal.z * helper.x - normal.x * helper.z,
        normal.x * helper.y - normal.y * helper.x,
    });
    Vector3 bitangent = Vector3{
        normal.y * tangent.z - normal.z * tangent.y,
        normal.z * tangent.x - normal.x * tangent.z,
        normal.x * tangent.y - normal.y * tangent.x,
    };
    const float r = std::sqrt(u1);
    const float theta = 2.0f * PI * u2;
    const float x = r * std::cos(theta);
    const float y = r * std::sin(theta);
    const float z = std::sqrt(std::max(0.0f, 1.0f - u1));
    return normalize3(add3(add3(scale3(tangent, x), scale3(bitangent, y)), scale3(normal, z)));
}

unsigned char tonemapByte(float value) {
    const float mapped = value / (1.0f + value);
    return static_cast<unsigned char>(std::clamp(mapped * 255.0f, 0.0f, 255.0f));
}

void logStage(const char* message) {
    TraceLog(LOG_INFO, "sloprad: %s", message);
    std::fflush(stdout);
}

void logProgress(const char* stage, std::size_t done, std::size_t total) {
    if (total == 0) {
        return;
    }
    const int percent = static_cast<int>((done * 100) / total);
    TraceLog(LOG_INFO, "sloprad: %s %zu/%zu (%d%%)", stage, done, total, percent);
    std::fflush(stdout);
}

template <typename Fn>
void parallelFor(std::size_t count, Fn&& fn) {
    if (count == 0) {
        return;
    }
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t threadCount = std::min<std::size_t>(hw, count);
    if (threadCount == 1) {
        fn(0, count);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    const std::size_t chunk = (count + threadCount - 1) / threadCount;
    for (std::size_t t = 0; t < threadCount; ++t) {
        const std::size_t begin = t * chunk;
        if (begin >= count) {
            break;
        }
        const std::size_t end = std::min(count, begin + chunk);
        threads.emplace_back([&, begin, end]() { fn(begin, end); });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}

Color3 luxelAt(
    const FaceLuxelGrid& grid,
    const std::vector<Color3>& previous,
    int x,
    int y,
    Color3 fallback) {
    x = std::clamp(x, 0, grid.luxelWidth - 1);
    y = std::clamp(y, 0, grid.luxelHeight - 1);
    const std::size_t index =
        grid.luxelBase + static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.luxelWidth)
        + static_cast<std::size_t>(x);
    if (index >= previous.size()) {
        return fallback;
    }
    return previous[index];
}

Color3 sampleFaceRadiance(
    const FaceLuxelGrid& grid,
    const FaceBasis& basis,
    const std::vector<Color3>& previous,
    Vector3 point,
    Color3 fallback) {
    if (!grid.valid || grid.luxelWidth <= 0 || grid.luxelHeight <= 0) {
        return fallback;
    }
    const float uSpan = basis.uMax - basis.uMin;
    const float vSpan = basis.vMax - basis.vMin;
    if (uSpan < 1e-8f || vSpan < 1e-8f) {
        return fallback;
    }
    const float u = dot3(point, basis.uAxis);
    const float v = dot3(point, basis.vAxis);
    const float fu = (u - basis.uMin) / uSpan;
    const float fv = (v - basis.vMin) / vSpan;
    const float fx = fu * static_cast<float>(std::max(grid.luxelWidth - 1, 0));
    const float fy = fv * static_cast<float>(std::max(grid.luxelHeight - 1, 0));
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const Color3 c00 = luxelAt(grid, previous, x0, y0, fallback);
    const Color3 c10 = luxelAt(grid, previous, x0 + 1, y0, fallback);
    const Color3 c01 = luxelAt(grid, previous, x0, y0 + 1, fallback);
    const Color3 c11 = luxelAt(grid, previous, x0 + 1, y0 + 1, fallback);
    const Color3 c0 = c00 * (1.0f - tx) + c10 * tx;
    const Color3 c1 = c01 * (1.0f - tx) + c11 * tx;
    return c0 * (1.0f - ty) + c1 * ty;
}

std::string brushIdFromFaceId(const std::string& faceId) {
    const auto slash = faceId.rfind('/');
    if (slash == std::string::npos) {
        return faceId;
    }
    return faceId.substr(0, slash);
}

struct EmitterVolume {
    std::string brushId;
    Vector3 mins{};
    Vector3 maxs{};
};

bool pointInAabb(Vector3 p, Vector3 mins, Vector3 maxs) {
    return p.x >= mins.x && p.x <= maxs.x && p.y >= mins.y && p.y <= maxs.y && p.z >= mins.z
        && p.z <= maxs.z;
}

std::vector<EmitterVolume> buildEmitterVolumes(
    const std::vector<LightmapFace>& faces,
    const std::unordered_map<std::string, MaterialBakeInfo>& materialCache,
    float pad) {
    struct Acc {
        Vector3 mins{};
        Vector3 maxs{};
        bool any = false;
    };
    std::unordered_map<std::string, Acc> byBrush;
    for (const LightmapFace& face : faces) {
        auto matIt = materialCache.find(face.material);
        if (matIt == materialCache.end()) {
            continue;
        }
        const MaterialBakeInfo& material = matIt->second;
        if (material.asset.sky) {
            continue;
        }
        if (material.asset.emissionPower <= 0.0f
            && material.asset.emissionColor.r == 0 && material.asset.emissionColor.g == 0
            && material.asset.emissionColor.b == 0 && !material.hasEmissionImage) {
            continue;
        }
        if (face.vertices.size() < 3
            || (luminance(emissionAt(face, material, face.vertices[0])) <= 0.0f
                && luminance(
                       emissionAt(face, material, face.vertices[face.vertices.size() / 2]))
                    <= 0.0f)) {
            continue;
        }
        Acc& acc = byBrush[brushIdFromFaceId(face.id)];
        for (const Vector3& c : face.vertices) {
            if (!acc.any) {
                acc.mins = acc.maxs = c;
                acc.any = true;
            } else {
                acc.mins = {std::min(acc.mins.x, c.x), std::min(acc.mins.y, c.y), std::min(acc.mins.z, c.z)};
                acc.maxs = {std::max(acc.maxs.x, c.x), std::max(acc.maxs.y, c.y), std::max(acc.maxs.z, c.z)};
            }
        }
    }

    std::vector<EmitterVolume> volumes;
    volumes.reserve(byBrush.size());
    for (auto& [brushId, acc] : byBrush) {
        if (!acc.any) {
            continue;
        }
        EmitterVolume volume;
        volume.brushId = brushId;
        volume.mins = {acc.mins.x - pad, acc.mins.y - pad, acc.mins.z - pad};
        volume.maxs = {acc.maxs.x + pad, acc.maxs.y + pad, acc.maxs.z + pad};
        volumes.push_back(std::move(volume));
    }
    return volumes;
}

bool luxelInsideForeignEmitter(
    Vector3 worldPos,
    const std::string& faceBrushId,
    const std::vector<EmitterVolume>& volumes) {
    for (const EmitterVolume& volume : volumes) {
        if (volume.brushId == faceBrushId) {
            continue;
        }
        if (pointInAabb(worldPos, volume.mins, volume.maxs)) {
            return true;
        }
    }
    return false;
}

struct LuxelSample {
    Vector3 position{};
    Vector3 normal{};
    Color3 albedo{};
    Color3 irradiance{};
    Color3 emission{};
    std::int32_t faceIndex = -1;
    std::int32_t interiorLeaf = -1;
    std::int32_t atlasIndex = 0;
    int atlasX = 0;
    int atlasY = 0;
    int localX = 0;
    int localY = 0;
    bool covered = false;
};

float wrapCosine(float cosine, float wrap) {
    const float w = std::max(0.0f, wrap);
    return std::max(0.0f, (cosine + w) / (1.0f + w));
}

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool faceIsSky(
    const std::vector<char>& faceSky,
    std::int32_t faceIndex) {
    return faceIndex >= 0 && static_cast<std::size_t>(faceIndex) < faceSky.size()
        && faceSky[static_cast<std::size_t>(faceIndex)] != 0;
}

void accumulateEntityLight(
    LuxelSample& luxel,
    const RadiosityLight& light,
    std::int32_t lightLeaf,
    const QuadBvh& occlusionBvh,
    const LeafReachability& reach,
    float wrap,
    float minDist2,
    const std::vector<char>& faceSky,
    const std::vector<char>& faceTransparent) {
    if (!reach.canSee(luxel.interiorLeaf, lightLeaf)) {
        return;
    }

    const Color3 intensity{
        light.color.x * light.intensity,
        light.color.y * light.intensity,
        light.color.z * light.intensity,
    };

    if (light.kind == RadiosityLightKind::Sun) {
        const float forwardLen = std::sqrt(dot3(light.direction, light.direction));
        if (forwardLen < 1e-6f) {
            return;
        }
        const Vector3 toLight = scale3(light.direction, -1.0f / forwardLen);
        const float nDotL = wrapCosine(dot3(luxel.normal, toLight), wrap);
        if (nDotL <= 0.0f) {
            return;
        }
        constexpr float kSunRayDistance = 1000.0f;
        const auto hit = raycastQuadBvh(
            occlusionBvh,
            luxel.position,
            toLight,
            kSunRayDistance,
            luxel.faceIndex,
            &faceTransparent);
        if (!hit || !faceIsSky(faceSky, hit->faceIndex)) {
            return;
        }
        luxel.irradiance += intensity * nDotL;
        return;
    }

    Vector3 delta = sub3(light.position, luxel.position);
    const float dist2Raw = dot3(delta, delta);
    if (dist2Raw < 1e-6f) {
        return;
    }
    const float dist = std::sqrt(dist2Raw);
    const float range = std::max(light.range, 1e-4f);
    if (dist > range) {
        return;
    }
    const Vector3 toLight = scale3(delta, 1.0f / dist);
    const float dist2 = std::max(dist2Raw, minDist2);
    const float nDotL = wrapCosine(dot3(luxel.normal, toLight), wrap);
    if (nDotL <= 0.0f) {
        return;
    }

    float spot = 1.0f;
    if (light.kind == RadiosityLightKind::Spot) {
        const float forwardLen = std::sqrt(dot3(light.direction, light.direction));
        if (forwardLen < 1e-6f) {
            return;
        }
        const Vector3 forward = scale3(light.direction, 1.0f / forwardLen);
        const float cosTheta = dot3(scale3(toLight, -1.0f), forward);
        const float cosOuter = std::cos(light.coneAngle);
        const float cosInner = std::cos(light.coneAngle * 0.8f);
        spot = smoothstep(cosOuter, cosInner, cosTheta);
        if (spot <= 0.0f) {
            return;
        }
    }

    if (bspSegmentOccluded(
            occlusionBvh,
            luxel.position,
            light.position,
            luxel.faceIndex,
            -1,
            &faceTransparent)) {
        return;
    }

    const float t = dist / range;
    float atten = std::max(0.0f, 1.0f - t * t);
    atten *= atten;

    luxel.irradiance += intensity * (nDotL * atten * spot / dist2);
}

void accumulateDirectLightingCpu(
    std::vector<LuxelSample>& luxels,
    const std::vector<EmitterPatch>& emitters,
    const std::vector<RadiosityLight>& lights,
    const std::vector<std::int32_t>& lightLeaves,
    const QuadBvh& occlusionBvh,
    const LeafReachability& reach,
    const RadiositySettings& settings,
    const std::vector<char>& faceSky,
    const std::vector<char>& faceTransparent) {
    const std::size_t luxelTotal = luxels.size();
    std::atomic<std::size_t> directDone{0};
    const std::size_t directStep = std::max<std::size_t>(1, luxelTotal / 20);
    const float wrap = settings.directWrap;
    const float coplanarFill = std::max(0.0f, settings.coplanarFill);
    const float luxelPitch = 1.0f / std::max(settings.luxelsPerMeter, 1e-3f);
    const float minDist2 = std::max(luxelPitch * luxelPitch, 0.0025f);
    constexpr float kCoplanarSoft = 0.25f;
    constexpr float kCoplanarAlignMin = 0.85f;
    parallelFor(luxelTotal, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            LuxelSample& luxel = luxels[i];
            if (luxel.covered) {
                const std::size_t done = directDone.fetch_add(1, std::memory_order_relaxed) + 1;
                if (done % directStep == 0 || done == luxelTotal) {
                    logProgress("direct", done, luxelTotal);
                }
                continue;
            }
            for (const EmitterPatch& emitter : emitters) {
                if (emitter.faceIndex == luxel.faceIndex) {
                    continue;
                }
                if (!reach.canSee(luxel.interiorLeaf, emitter.interiorLeaf)) {
                    continue;
                }
                Vector3 delta = sub3(emitter.position, luxel.position);
                const float dist2Raw = dot3(delta, delta);
                if (dist2Raw < 1e-6f) {
                    continue;
                }
                const float dist = std::sqrt(dist2Raw);
                const Vector3 toLight = scale3(delta, 1.0f / dist);
                const float dist2 = std::max(dist2Raw, minDist2);
                const float nDotL = wrapCosine(dot3(luxel.normal, toLight), wrap);
                const float nDotV = wrapCosine(-dot3(emitter.normal, toLight), wrap);
                const bool formOk = nDotL > 0.0f && nDotV > 0.0f;
                float align = 0.0f;
                bool fillOk = false;
                if (coplanarFill > 0.0f) {
                    align = dot3(luxel.normal, emitter.normal);
                    fillOk = align > kCoplanarAlignMin;
                }
                if (!formOk && !fillOk) {
                    continue;
                }
                if (bspSegmentOccluded(
                        occlusionBvh,
                        luxel.position,
                        emitter.position,
                        luxel.faceIndex,
                        emitter.faceIndex,
                        &faceTransparent)) {
                    continue;
                }

                if (formOk) {
                    const float form = nDotL * nDotV * emitter.area / (dist2 * PI);
                    luxel.irradiance += emitter.radiance * form;
                }

                if (fillOk) {
                    const float planeSep = std::fabs(dot3(delta, luxel.normal));
                    const float lateral2 = std::max(0.0f, dist2Raw - planeSep * planeSep);
                    const float weight = align * std::exp(-planeSep / kCoplanarSoft)
                        / (lateral2 + minDist2);
                    const float fill = emitter.area * coplanarFill * weight / (4.0f * PI);
                    luxel.irradiance += emitter.radiance * fill;
                }
            }
            for (std::size_t li = 0; li < lights.size(); ++li) {
                const std::int32_t lightLeaf =
                    li < lightLeaves.size() ? lightLeaves[li] : static_cast<std::int32_t>(-1);
                accumulateEntityLight(
                    luxel,
                    lights[li],
                    lightLeaf,
                    occlusionBvh,
                    reach,
                    wrap,
                    minDist2,
                    faceSky,
                    faceTransparent);
            }
            const std::size_t done = directDone.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done % directStep == 0 || done == luxelTotal) {
                logProgress("direct", done, luxelTotal);
            }
        }
    });
}

bool accumulateDirectLighting(
    std::vector<LuxelSample>& luxels,
    const std::vector<EmitterPatch>& emitters,
    const std::vector<RadiosityLight>& lights,
    const std::vector<std::int32_t>& lightLeaves,
    const QuadBvh& occlusionBvh,
    const LeafReachability& reach,
    const RadiositySettings& settings,
    const std::vector<char>& faceSky,
    const std::vector<char>& faceTransparent) {
    RadGpuReachability gpuReach;
    if (reach.enabled) {
        gpuReach.leafCount = reach.leafCount;
        gpuReach.wordsPerRow = reach.wordsPerRow;
        gpuReach.bits = reach.bits;
    }
    if (settings.preferGpu && !settings.directComputeShaderSource.empty()) {
        std::vector<RadGpuLuxel> gpuLuxels;
        std::vector<std::size_t> denseToSrc;
        gpuLuxels.reserve(luxels.size());
        denseToSrc.reserve(luxels.size());
        for (std::size_t i = 0; i < luxels.size(); ++i) {
            const LuxelSample& src = luxels[i];
            if (src.covered) {
                continue;
            }
            RadGpuLuxel dst;
            dst.position = src.position;
            dst.normal = src.normal;
            dst.irradianceR = src.irradiance.r;
            dst.irradianceG = src.irradiance.g;
            dst.irradianceB = src.irradiance.b;
            dst.faceIndex = src.faceIndex;
            dst.covered = 0;
            dst.interiorLeaf = src.interiorLeaf;
            gpuLuxels.push_back(dst);
            denseToSrc.push_back(i);
        }
        std::vector<RadGpuEmitter> gpuEmitters(emitters.size());
        for (std::size_t i = 0; i < emitters.size(); ++i) {
            const EmitterPatch& src = emitters[i];
            RadGpuEmitter& dst = gpuEmitters[i];
            dst.position = src.position;
            dst.normal = src.normal;
            dst.radianceR = src.radiance.r;
            dst.radianceG = src.radiance.g;
            dst.radianceB = src.radiance.b;
            dst.area = src.area;
            dst.faceIndex = src.faceIndex;
            dst.interiorLeaf = src.interiorLeaf;
        }
        std::vector<RadGpuLight> gpuLights(lights.size());
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const RadiosityLight& src = lights[i];
            RadGpuLight& dst = gpuLights[i];
            dst.position = src.position;
            dst.direction = src.direction;
            dst.color = src.color;
            dst.intensity = src.intensity;
            dst.range = src.range;
            dst.coneAngle = src.coneAngle;
            if (src.kind == RadiosityLightKind::Spot) {
                dst.kind = 1;
            } else if (src.kind == RadiosityLightKind::Sun) {
                dst.kind = 2;
            } else {
                dst.kind = 0;
            }
            dst.interiorLeaf =
                i < lightLeaves.size() ? lightLeaves[i] : static_cast<std::int32_t>(-1);
        }
        RadGpuDirectParams gpuParams;
        gpuParams.directWrap = settings.directWrap;
        gpuParams.coplanarFill = settings.coplanarFill;
        gpuParams.coplanarSoft = 0.25f;
        const float luxelPitch = 1.0f / std::max(settings.luxelsPerMeter, 1e-3f);
        gpuParams.minDist2 = std::max(luxelPitch * luxelPitch, 0.0025f);
        std::vector<std::int32_t> faceIsSky(faceSky.size(), 0);
        for (std::size_t i = 0; i < faceSky.size(); ++i) {
            faceIsSky[i] = faceSky[i] != 0 ? 1 : 0;
        }
        std::vector<std::int32_t> faceIsTransparent(faceTransparent.size(), 0);
        for (std::size_t i = 0; i < faceTransparent.size(); ++i) {
            faceIsTransparent[i] = faceTransparent[i] != 0 ? 1 : 0;
        }
        if (accumulateDirectLightingGpu(
                gpuLuxels,
                gpuEmitters,
                gpuLights,
                occlusionBvh,
                settings.directComputeShaderSource,
                gpuParams,
                gpuReach,
                faceIsSky,
                faceIsTransparent)) {
            for (std::size_t d = 0; d < gpuLuxels.size(); ++d) {
                LuxelSample& dst = luxels[denseToSrc[d]];
                dst.irradiance.r = gpuLuxels[d].irradianceR;
                dst.irradiance.g = gpuLuxels[d].irradianceG;
                dst.irradiance.b = gpuLuxels[d].irradianceB;
            }
            return true;
        }
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting failed; falling back to CPU");
        std::fflush(stdout);
    } else if (settings.preferGpu) {
        TraceLog(LOG_WARNING, "sloprad: GPU direct lighting requested but shader source missing; using CPU");
        std::fflush(stdout);
    }

    TraceLog(LOG_INFO, "sloprad: CPU direct lighting");
    std::fflush(stdout);
    accumulateDirectLightingCpu(
        luxels,
        emitters,
        lights,
        lightLeaves,
        occlusionBvh,
        reach,
        settings,
        faceSky,
        faceTransparent);
    return false;
}

void inpaintCoveredLuxels(
    std::vector<LuxelSample>& luxels,
    const std::vector<FaceLuxelGrid>& faceGrids) {
    std::vector<char> hole(luxels.size(), 0);
    std::size_t holeCount = 0;
    for (std::size_t i = 0; i < luxels.size(); ++i) {
        if (luxels[i].covered) {
            hole[i] = 1;
            ++holeCount;
        }
    }
    if (holeCount == 0) {
        return;
    }

    constexpr int kMaxPasses = 64;
    const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int pass = 0; pass < kMaxPasses && holeCount > 0; ++pass) {
        std::vector<Color3> fill(luxels.size());
        std::vector<char> fillMask(luxels.size(), 0);
        for (std::size_t i = 0; i < luxels.size(); ++i) {
            if (!hole[i]) {
                continue;
            }
            const LuxelSample& luxel = luxels[i];
            if (luxel.faceIndex < 0
                || luxel.faceIndex >= static_cast<std::int32_t>(faceGrids.size())) {
                continue;
            }
            const FaceLuxelGrid& grid = faceGrids[static_cast<std::size_t>(luxel.faceIndex)];
            if (!grid.valid) {
                continue;
            }
            Color3 sum{};
            int count = 0;
            for (const auto& offset : offsets) {
                const int nx = luxel.localX + offset[0];
                const int ny = luxel.localY + offset[1];
                if (nx < 0 || ny < 0 || nx >= grid.luxelWidth || ny >= grid.luxelHeight) {
                    continue;
                }
                const std::size_t ni =
                    grid.luxelBase + static_cast<std::size_t>(ny) * static_cast<std::size_t>(grid.luxelWidth)
                    + static_cast<std::size_t>(nx);
                if (ni >= luxels.size() || hole[ni] != 0) {
                    continue;
                }
                sum += luxels[ni].irradiance;
                ++count;
            }
            if (count > 0) {
                fill[i] = sum * (1.0f / static_cast<float>(count));
                fillMask[i] = 1;
            }
        }
        for (std::size_t i = 0; i < luxels.size(); ++i) {
            if (fillMask[i] == 0) {
                continue;
            }
            luxels[i].irradiance = fill[i];
            hole[i] = 0;
            --holeCount;
        }
    }
}

void bilateralDenoiseLuxels(
    std::vector<LuxelSample>& luxels,
    const std::vector<FaceLuxelGrid>& faceGrids) {
    constexpr float kSpatialSigma = 1.0f;
    constexpr float kRangeSigma = 0.35f;
    const float invTwoSpatial = 1.0f / (2.0f * kSpatialSigma * kSpatialSigma);
    const float invTwoRange = 1.0f / (2.0f * kRangeSigma * kRangeSigma);
    std::vector<Color3> filtered(luxels.size());
    std::vector<char> writeMask(luxels.size(), 0);
    for (const FaceLuxelGrid& grid : faceGrids) {
        if (!grid.valid || grid.luxelWidth <= 0 || grid.luxelHeight <= 0) {
            continue;
        }
        for (int y = 0; y < grid.luxelHeight; ++y) {
            for (int x = 0; x < grid.luxelWidth; ++x) {
                const std::size_t center =
                    grid.luxelBase + static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.luxelWidth)
                    + static_cast<std::size_t>(x);
                if (center >= luxels.size() || luxels[center].covered) {
                    continue;
                }
                const float centerLum = luminance(luxels[center].irradiance);
                Color3 sum{};
                float weightSum = 0.0f;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (nx < 0 || ny < 0 || nx >= grid.luxelWidth || ny >= grid.luxelHeight) {
                            continue;
                        }
                        const std::size_t ni =
                            grid.luxelBase
                            + static_cast<std::size_t>(ny) * static_cast<std::size_t>(grid.luxelWidth)
                            + static_cast<std::size_t>(nx);
                        if (ni >= luxels.size() || luxels[ni].covered) {
                            continue;
                        }
                        const float dist2 = static_cast<float>(ox * ox + oy * oy);
                        const float lumDelta = luminance(luxels[ni].irradiance) - centerLum;
                        const float w = std::exp(-dist2 * invTwoSpatial - lumDelta * lumDelta * invTwoRange);
                        sum += luxels[ni].irradiance * w;
                        weightSum += w;
                    }
                }
                if (weightSum > 0.0f) {
                    filtered[center] = sum * (1.0f / weightSum);
                    writeMask[center] = 1;
                }
            }
        }
    }
    for (std::size_t i = 0; i < luxels.size(); ++i) {
        if (writeMask[i] != 0) {
            luxels[i].irradiance = filtered[i];
        }
    }
}

void accumulateBounceLightingCpu(
    std::vector<LuxelSample>& luxels,
    std::vector<Color3>& gatheredLuxels,
    const std::vector<Color3>& shoot,
    const std::vector<FaceLuxelGrid>& faceGrids,
    const std::vector<FaceBasis>& bases,
    const QuadBvh& sceneBvh,
    const Color3& ambientRaw,
    int sampleCount,
    int bounceIndex,
    int bounceTotal,
    const std::vector<char>& faceTransparent) {
    std::atomic<std::size_t> bounceDone{0};
    const std::size_t bounceStep = std::max<std::size_t>(1, luxels.size() / 20);
    const int strataN = std::max(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(sampleCount)))));
    const int strataM = std::max(1, (sampleCount + strataN - 1) / strataN);
    parallelFor(luxels.size(), [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            LuxelSample& luxel = luxels[i];
            if (luxel.covered) {
                const std::size_t done = bounceDone.fetch_add(1, std::memory_order_relaxed) + 1;
                if (done % bounceStep == 0 || done == luxels.size()) {
                    TraceLog(
                        LOG_INFO,
                        "sloprad: bounce %d/%d luxels %zu/%zu (%d%%)",
                        bounceIndex + 1,
                        bounceTotal,
                        done,
                        luxels.size(),
                        static_cast<int>((done * 100) / luxels.size()));
                    std::fflush(stdout);
                }
                continue;
            }
            std::mt19937 rng(hashLuxelSeed(luxel.faceIndex, luxel.localX, luxel.localY) ^ 0x9e3779b9u);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            Color3 gathered{};
            int fired = 0;
            for (int sy = 0; sy < strataM && fired < sampleCount; ++sy) {
                for (int sx = 0; sx < strataN && fired < sampleCount; ++sx) {
                    const float u1 = (static_cast<float>(sx) + unit(rng)) / static_cast<float>(strataN);
                    const float u2 = (static_cast<float>(sy) + unit(rng)) / static_cast<float>(strataM);
                    const Vector3 dir = cosineHemisphere(luxel.normal, u1, u2);
                    const auto hit = raycastQuadBvh(
                        sceneBvh,
                        luxel.position,
                        dir,
                        1000.0f,
                        luxel.faceIndex,
                        &faceTransparent);
                    ++fired;
                    if (!hit) {
                        continue;
                    }
                    Color3 hitRadiance = ambientRaw * 0.25f;
                    if (hit->faceIndex >= 0
                        && hit->faceIndex < static_cast<std::int32_t>(faceGrids.size())) {
                        hitRadiance = sampleFaceRadiance(
                            faceGrids[static_cast<std::size_t>(hit->faceIndex)],
                            bases[static_cast<std::size_t>(hit->faceIndex)],
                            shoot,
                            hit->point,
                            hitRadiance);
                    }
                    gathered += hitRadiance;
                }
            }
            gathered = gathered * (1.0f / static_cast<float>(sampleCount));
            gatheredLuxels[i] = gathered;
            luxel.irradiance += gathered;
            const std::size_t done = bounceDone.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done % bounceStep == 0 || done == luxels.size()) {
                TraceLog(
                    LOG_INFO,
                    "sloprad: bounce %d/%d luxels %zu/%zu (%d%%)",
                    bounceIndex + 1,
                    bounceTotal,
                    done,
                    luxels.size(),
                    static_cast<int>((done * 100) / luxels.size()));
                std::fflush(stdout);
            }
        }
    });
}

} // namespace

RadiosityBakeResult bakeRadiosity(
    const std::vector<LightmapFace>& faces,
    const MapMeta& meta,
    const MaterialBakeResolver& resolveMaterial,
    const RadiositySettings& settings,
    const std::vector<RadiosityLight>& lights,
    const BspTree* tree,
    bool hullSealed) {
    int pointCount = 0;
    int spotCount = 0;
    int sunCount = 0;
    for (const RadiosityLight& light : lights) {
        if (light.kind == RadiosityLightKind::Spot) {
            ++spotCount;
        } else if (light.kind == RadiosityLightKind::Sun) {
            ++sunCount;
        } else {
            ++pointCount;
        }
    }
    TraceLog(
        LOG_INFO,
        "sloprad: bake start faces=%d luxels/m=%.1f bounces=%d samples=%d atlas=%d ambient=(%.3f %.3f %.3f) lights=%d (point=%d spot=%d sun=%d)",
        static_cast<int>(faces.size()),
        settings.luxelsPerMeter,
        settings.bounces,
        settings.samples,
        settings.atlasSize,
        meta.ambient.x,
        meta.ambient.y,
        meta.ambient.z,
        static_cast<int>(lights.size()),
        pointCount,
        spotCount,
        sunCount);
    std::fflush(stdout);

    LeafReachability reach;
    if (hullSealed && tree != nullptr) {
        reach = buildOpenLeafReachability(*tree);
        TraceLog(
            LOG_INFO,
            "sloprad: leaf reachability enabled leaves=%d",
            reach.leafCount);
        std::fflush(stdout);
    } else {
        TraceLog(LOG_INFO, "sloprad: leaf reachability disabled");
        std::fflush(stdout);
    }

    std::unordered_map<std::string, MaterialBakeInfo> materialCache;
    auto materialFor = [&](const std::string& path) -> const MaterialBakeInfo& {
        auto it = materialCache.find(path);
        if (it == materialCache.end()) {
            it = materialCache.emplace(path, resolveMaterial(path)).first;
            const MaterialBakeInfo& info = it->second;
            TraceLog(
                LOG_INFO,
                "sloprad: material '%s' emissionPower=%.2f emissionMap=%s sky=%s",
                path.c_str(),
                info.asset.emissionPower,
                info.hasEmissionImage ? "yes" : "no",
                info.asset.sky ? "yes" : "no");
            std::fflush(stdout);
        }
        return it->second;
    };

    std::vector<char> faceSky(faces.size(), 0);
    int skyFaceCount = 0;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (materialFor(faces[i].material).asset.sky) {
            faceSky[i] = 1;
            ++skyFaceCount;
        }
    }
    std::vector<char> faceTransparent(faces.size(), 0);
    int transparentFaceCount = 0;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (faces[i].transparent) {
            faceTransparent[i] = 1;
            ++transparentFaceCount;
        }
    }
    TraceLog(LOG_INFO, "sloprad: sky faces=%d transparent faces=%d", skyFaceCount, transparentFaceCount);
    std::fflush(stdout);
    const bool hasSunLight = std::any_of(
        lights.begin(),
        lights.end(),
        [](const RadiosityLight& light) { return light.kind == RadiosityLightKind::Sun; });
    if (hasSunLight && skyFaceCount == 0) {
        TraceLog(
            LOG_WARNING,
            "sloprad: sun thing present but no sky-material faces; directional sun will not apply");
        std::fflush(stdout);
    }

    logStage("packing lightmap charts...");
    LightmapPackResult packed =
        packLightmapCharts(faces, settings.luxelsPerMeter, settings.atlasSize, &faceSky);
    RadiosityBakeResult result;
    result.rad = packed.rad;
    TraceLog(
        LOG_INFO,
        "sloprad: packed charts=%d atlases=%d",
        static_cast<int>(packed.rad.charts.size()),
        static_cast<int>(packed.rad.atlases.size()));
    std::fflush(stdout);

    std::vector<FaceBasis> bases(faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        bases[i] = makeFaceBasis(faces[i]);
    }

    const float coveragePad = 0.5f / std::max(settings.luxelsPerMeter, 1e-3f);
    const std::vector<EmitterVolume> emitterVolumes =
        buildEmitterVolumes(faces, materialCache, coveragePad);
    TraceLog(LOG_INFO, "sloprad: emitter volumes=%d", static_cast<int>(emitterVolumes.size()));
    std::fflush(stdout);

    logStage("building acceleration structures...");
    const QuadBvh sceneBvh = buildLightmapFaceBvh(faces);
    TraceLog(
        LOG_INFO,
        "sloprad: scene prims=%d threads=%u",
        static_cast<int>(sceneBvh.prims.size()),
        std::max(1u, std::thread::hardware_concurrency()));
    std::fflush(stdout);

    logStage("collecting emitter patches...");
    std::vector<EmitterPatch> emitters;
    int emitterCharts = 0;
    for (std::size_t chartIndex = 0; chartIndex < packed.rad.charts.size(); ++chartIndex) {
        const LightmapChart& chart = packed.rad.charts[chartIndex];
        if (chart.faceIndex < 0 || chart.faceIndex >= static_cast<std::int32_t>(faces.size())) {
            continue;
        }
        const LightmapFace& face = faces[static_cast<std::size_t>(chart.faceIndex)];
        const MaterialBakeInfo& material = materialFor(face.material);
        if (material.asset.sky) {
            continue;
        }
        const FaceBasis& basis = bases[static_cast<std::size_t>(chart.faceIndex)];
        const float luxelArea =
            ((basis.uMax - basis.uMin) * (basis.vMax - basis.vMin))
            / static_cast<float>(std::max(1, chart.luxelWidth * chart.luxelHeight));

        const std::size_t emittersBefore = emitters.size();
        for (int y = 0; y < chart.luxelHeight; ++y) {
            for (int x = 0; x < chart.luxelWidth; ++x) {
                const float fu = luxelFaceParam(x, chart.luxelWidth);
                const float fv = luxelFaceParam(y, chart.luxelHeight);
                const float u = basis.uMin + (basis.uMax - basis.uMin) * fu;
                const float v = basis.vMin + (basis.vMax - basis.vMin) * fv;
                if (!pointInFacePolygon(face, basis, u, v)) {
                    continue;
                }
                const Vector3 pos = luxelWorldPos(basis, chart, x, y);
                const Color3 emit = emissionAt(face, material, pos);
                if (luminance(emit) <= 0.0f) {
                    continue;
                }
                EmitterPatch patch;
                patch.position = add3(pos, scale3(face.normal, 0.02f));
                patch.normal = face.normal;
                patch.radiance = emit;
                patch.area = luxelArea;
                patch.faceIndex = chart.faceIndex;
                patch.interiorLeaf = face.interiorLeaf;
                if (patch.interiorLeaf < 0 && tree != nullptr) {
                    patch.interiorLeaf = pointLeaf(*tree, patch.position);
                }
                emitters.push_back(patch);
            }
        }
        if (emitters.size() > emittersBefore) {
            ++emitterCharts;
        }
        if ((chartIndex + 1) % 32 == 0 || chartIndex + 1 == packed.rad.charts.size()) {
            logProgress("emitters from charts", chartIndex + 1, packed.rad.charts.size());
        }
    }
    TraceLog(
        LOG_INFO,
        "sloprad: emitter patches=%d charts=%d",
        static_cast<int>(emitters.size()),
        emitterCharts);
    std::fflush(stdout);

    std::vector<std::int32_t> lightLeaves(lights.size(), -1);
    if (tree != nullptr) {
        for (std::size_t i = 0; i < lights.size(); ++i) {
            if (lights[i].kind == RadiosityLightKind::Sun) {
                continue;
            }
            lightLeaves[i] = pointLeaf(*tree, lights[i].position);
        }
    }

    const Color3 ambientRaw{meta.ambient.x, meta.ambient.y, meta.ambient.z};
    const Color3 ambient = ambientRaw * std::max(0.0f, settings.ambientScale);

    logStage("building receiving luxels...");
    std::vector<LuxelSample> luxels;
    luxels.reserve(packed.rad.charts.size() * 16);
    std::vector<FaceLuxelGrid> faceGrids(faces.size());
    std::size_t coveredCount = 0;
    for (const LightmapChart& chart : packed.rad.charts) {
        if (chart.faceIndex < 0 || chart.faceIndex >= static_cast<std::int32_t>(faces.size())) {
            continue;
        }
        const LightmapFace& face = faces[static_cast<std::size_t>(chart.faceIndex)];
        const MaterialBakeInfo& material = materialFor(face.material);
        const FaceBasis& basis = bases[static_cast<std::size_t>(chart.faceIndex)];
        const std::string faceBrushId = brushIdFromFaceId(face.id);

        FaceLuxelGrid& grid = faceGrids[static_cast<std::size_t>(chart.faceIndex)];
        grid.valid = true;
        grid.luxelBase = luxels.size();
        grid.luxelWidth = chart.luxelWidth;
        grid.luxelHeight = chart.luxelHeight;

        for (int y = 0; y < chart.luxelHeight; ++y) {
            for (int x = 0; x < chart.luxelWidth; ++x) {
                LuxelSample sample;
                const Vector3 pos = luxelWorldPos(basis, chart, x, y);
                sample.position = add3(pos, scale3(face.normal, 0.02f));
                sample.normal = face.normal;
                sample.albedo = albedoAt(face, material, pos);
                sample.emission = emissionAt(face, material, pos);
                sample.irradiance = ambient + sample.emission;
                sample.faceIndex = chart.faceIndex;
                sample.interiorLeaf = face.interiorLeaf;
                if (sample.interiorLeaf < 0 && tree != nullptr) {
                    sample.interiorLeaf = pointLeaf(*tree, sample.position);
                }
                sample.atlasIndex = chart.atlasIndex;
                sample.atlasX = chart.atlasX + x;
                sample.atlasY = chart.atlasY + y;
                sample.localX = x;
                sample.localY = y;
                const float fu = luxelFaceParam(x, chart.luxelWidth);
                const float fv = luxelFaceParam(y, chart.luxelHeight);
                const float u = basis.uMin + (basis.uMax - basis.uMin) * fu;
                const float v = basis.vMin + (basis.vMax - basis.vMin) * fv;
                const bool outsidePoly = !pointInFacePolygon(face, basis, u, v);
                sample.covered = outsidePoly
                    || luxelInsideForeignEmitter(pos, faceBrushId, emitterVolumes);
                if (sample.covered) {
                    sample.irradiance = ambient;
                    ++coveredCount;
                }
                luxels.push_back(sample);
            }
        }
    }
    TraceLog(
        LOG_INFO,
        "sloprad: receiving luxels=%d covered=%d",
        static_cast<int>(luxels.size()),
        static_cast<int>(coveredCount));
    std::fflush(stdout);

    logStage("direct lighting...");
    TraceLog(
        LOG_INFO,
        "sloprad: direct contributors emitters=%d lights=%d",
        static_cast<int>(emitters.size()),
        static_cast<int>(lights.size()));
    std::fflush(stdout);
    accumulateDirectLighting(
        luxels,
        emitters,
        lights,
        lightLeaves,
        sceneBvh,
        reach,
        settings,
        faceSky,
        faceTransparent);

    logStage("inpainting covered luxels...");
    inpaintCoveredLuxels(luxels, faceGrids);

    std::vector<Color3> shoot(luxels.size());
    for (std::size_t i = 0; i < luxels.size(); ++i) {
        const LuxelSample& luxel = luxels[i];
        if (luxel.covered) {
            shoot[i] = {};
            continue;
        }
        shoot[i] = max0(luxel.irradiance - luxel.emission) * luxel.albedo;
    }

    for (int bounce = 0; bounce < settings.bounces; ++bounce) {
        TraceLog(LOG_INFO, "sloprad: bounce %d/%d...", bounce + 1, settings.bounces);
        std::fflush(stdout);

        std::vector<Color3> gatheredLuxels(luxels.size());
        const int sampleCount = std::max(1, settings.samples);
        bool usedGpuBounce = false;
        if (settings.preferGpu && !settings.bounceComputeShaderSource.empty()) {
            std::vector<RadGpuBounceLuxel> gpuLuxels(luxels.size());
            std::vector<Vector3> gatheredRgb(luxels.size());
            std::vector<Vector3> shootRgb(shoot.size());
            for (std::size_t i = 0; i < luxels.size(); ++i) {
                gpuLuxels[i].position = luxels[i].position;
                gpuLuxels[i].normal = luxels[i].normal;
                gpuLuxels[i].faceIndex = luxels[i].faceIndex;
                gpuLuxels[i].covered = luxels[i].covered ? 1 : 0;
                gpuLuxels[i].localX = luxels[i].localX;
                gpuLuxels[i].localY = luxels[i].localY;
                shootRgb[i] = {shoot[i].r, shoot[i].g, shoot[i].b};
            }
            std::vector<RadGpuFaceGrid> gpuGrids(faceGrids.size());
            for (std::size_t i = 0; i < faceGrids.size(); ++i) {
                gpuGrids[i].luxelBase = static_cast<std::int32_t>(faceGrids[i].luxelBase);
                gpuGrids[i].luxelWidth = faceGrids[i].luxelWidth;
                gpuGrids[i].luxelHeight = faceGrids[i].luxelHeight;
                gpuGrids[i].valid = faceGrids[i].valid ? 1 : 0;
                gpuGrids[i].uAxis = bases[i].uAxis;
                gpuGrids[i].vAxis = bases[i].vAxis;
                gpuGrids[i].uMin = bases[i].uMin;
                gpuGrids[i].uMax = bases[i].uMax;
                gpuGrids[i].vMin = bases[i].vMin;
                gpuGrids[i].vMax = bases[i].vMax;
            }
            RadGpuBounceParams bounceParams;
            bounceParams.sampleCount = sampleCount;
            bounceParams.ambientR = ambientRaw.r;
            bounceParams.ambientG = ambientRaw.g;
            bounceParams.ambientB = ambientRaw.b;
            bounceParams.seed = 0xA341316Cu ^ static_cast<std::uint32_t>(bounce * 0x9E3779B9u);
            if (accumulateBounceLightingGpu(
                    gpuLuxels,
                    gatheredRgb,
                    shootRgb,
                    gpuGrids,
                    sceneBvh,
                    settings.bounceComputeShaderSource,
                    bounceParams,
                    faceTransparent)) {
                usedGpuBounce = true;
                for (std::size_t i = 0; i < luxels.size(); ++i) {
                    if (luxels[i].covered) {
                        continue;
                    }
                    gatheredLuxels[i] = {gatheredRgb[i].x, gatheredRgb[i].y, gatheredRgb[i].z};
                    luxels[i].irradiance += gatheredLuxels[i];
                }
                TraceLog(LOG_INFO, "sloprad: GPU bounce %d/%d complete", bounce + 1, settings.bounces);
                std::fflush(stdout);
            } else {
                TraceLog(LOG_WARNING, "sloprad: GPU bounce failed; falling back to CPU");
                std::fflush(stdout);
            }
        }
        if (!usedGpuBounce) {
            accumulateBounceLightingCpu(
                luxels,
                gatheredLuxels,
                shoot,
                faceGrids,
                bases,
                sceneBvh,
                ambientRaw,
                sampleCount,
                bounce,
                settings.bounces,
                faceTransparent);
        }

        for (std::size_t i = 0; i < luxels.size(); ++i) {
            if (luxels[i].covered) {
                shoot[i] = {};
            } else {
                shoot[i] = gatheredLuxels[i] * luxels[i].albedo;
            }
        }
        inpaintCoveredLuxels(luxels, faceGrids);
        for (std::size_t i = 0; i < luxels.size(); ++i) {
            if (luxels[i].covered) {
                shoot[i] = {};
            }
        }
    }

    logStage("denoising irradiance...");
    bilateralDenoiseLuxels(luxels, faceGrids);

    logStage("rasterizing lightmap atlases...");
    for (std::size_t atlas = 0; atlas < packed.atlasRgb.size(); ++atlas) {
        Image image = GenImageColor(
            packed.rad.atlases[atlas].width,
            packed.rad.atlases[atlas].height,
            BLACK);
        result.atlasImages.push_back(image);
    }

    for (const LuxelSample& luxel : luxels) {
        if (luxel.atlasIndex < 0 || luxel.atlasIndex >= static_cast<std::int32_t>(result.atlasImages.size())) {
            continue;
        }
        Image& image = result.atlasImages[static_cast<std::size_t>(luxel.atlasIndex)];
        const Color pixel{
            tonemapByte(luxel.irradiance.r),
            tonemapByte(luxel.irradiance.g),
            tonemapByte(luxel.irradiance.b),
            255,
        };
        ImageDrawPixel(&image, luxel.atlasX, luxel.atlasY, pixel);
    }

    for (auto& [path, info] : materialCache) {
        (void)path;
        if (info.hasAlbedoImage) {
            UnloadImage(info.albedoImage);
        }
        if (info.hasEmissionImage) {
            UnloadImage(info.emissionImage);
        }
    }

    logStage("bake complete");
    return result;
}

}
