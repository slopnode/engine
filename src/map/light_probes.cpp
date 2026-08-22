#include "map/light_probes.hpp"

#include "map/bsp_ray.hpp"
#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace slopengine {

namespace {

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool leafIsOpenAt(const BspTree& tree, Vector3 point) {
    const std::int32_t leaf = pointLeaf(tree, point);
    return leaf >= 0 && leafIsEmpty(tree, leaf);
}

constexpr Vector3 kAxisOffsets[6] = {
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, -1.0f},
};

Vector3 nudgeAwayFromSolid(const BspTree& tree, Vector3 point, float spacing) {
    Vector3 push{0.0f, 0.0f, 0.0f};
    bool anySolidNeighbor = false;
    const float probeRadius = spacing * 0.5f;
    for (const Vector3& axis : kAxisOffsets) {
        const Vector3 sample = {
            point.x + axis.x * probeRadius,
            point.y + axis.y * probeRadius,
            point.z + axis.z * probeRadius,
        };
        if (!leafIsOpenAt(tree, sample)) {
            push.x -= axis.x;
            push.y -= axis.y;
            push.z -= axis.z;
            anySolidNeighbor = true;
        }
    }
    if (!anySolidNeighbor) {
        return point;
    }
    const float lenSq = push.x * push.x + push.y * push.y + push.z * push.z;
    if (lenSq < 1e-8f) {
        return point;
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    constexpr float kNudgeEpsilon = 0.15f;
    const Vector3 nudged = {
        point.x + push.x * invLen * kNudgeEpsilon,
        point.y + push.y * invLen * kNudgeEpsilon,
        point.z + push.z * invLen * kNudgeEpsilon,
    };
    return leafIsOpenAt(tree, nudged) ? nudged : point;
}

struct CoarseGrid {
    Vector3 mins{};
    int nx = 0;
    int ny = 0;
    int nz = 0;
    float cellSize = 4.0f;
    std::vector<char> open;

    std::size_t index(int ix, int iy, int iz) const {
        return (static_cast<std::size_t>(iz) * ny + iy) * nx + ix;
    }

    Vector3 center(int ix, int iy, int iz) const {
        return {
            mins.x + static_cast<float>(ix) * cellSize,
            mins.y + static_cast<float>(iy) * cellSize,
            mins.z + static_cast<float>(iz) * cellSize,
        };
    }

    bool isOpen(int ix, int iy, int iz) const {
        return open[index(ix, iy, iz)] != 0;
    }
};

CoarseGrid buildCoarseGrid(const BspTree& tree, float cellSize) {
    CoarseGrid grid;
    grid.cellSize = std::max(cellSize, 0.25f);
    grid.mins = tree.boundsMins;
    grid.nx = std::max(1, static_cast<int>(std::ceil((tree.boundsMaxs.x - tree.boundsMins.x) / grid.cellSize)) + 1);
    grid.ny = std::max(1, static_cast<int>(std::ceil((tree.boundsMaxs.y - tree.boundsMins.y) / grid.cellSize)) + 1);
    grid.nz = std::max(1, static_cast<int>(std::ceil((tree.boundsMaxs.z - tree.boundsMins.z) / grid.cellSize)) + 1);
    grid.open.assign(static_cast<std::size_t>(grid.nx) * grid.ny * grid.nz, 0);
    for (int iz = 0; iz < grid.nz; ++iz) {
        for (int iy = 0; iy < grid.ny; ++iy) {
            for (int ix = 0; ix < grid.nx; ++ix) {
                const bool open = leafIsOpenAt(tree, grid.center(ix, iy, iz));
                grid.open[grid.index(ix, iy, iz)] = open ? 1 : 0;
            }
        }
    }
    return grid;
}

bool isBoundaryCell(const CoarseGrid& grid, int ix, int iy, int iz) {
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            const int nix = ix + (axis == 0 ? sign : 0);
            const int niy = iy + (axis == 1 ? sign : 0);
            const int niz = iz + (axis == 2 ? sign : 0);
            if (nix < 0 || nix >= grid.nx || niy < 0 || niy >= grid.ny || niz < 0 || niz >= grid.nz) {
                continue;
            }
            if (!grid.isOpen(nix, niy, niz)) {
                return true;
            }
        }
    }
    return false;
}

Vector3 fibonacciSphereDirection(int index, int count) {
    constexpr float kGoldenAngle = 2.399963229728653f;
    const float t = (static_cast<float>(index) + 0.5f) / static_cast<float>(std::max(count, 1));
    const float y = 1.0f - 2.0f * t;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float theta = kGoldenAngle * static_cast<float>(index);
    return {radius * std::cos(theta), y, radius * std::sin(theta)};
}

Vector3 sampleChartLinearRadiance(
    const std::vector<LightmapFace>& faces,
    const RadFile& rad,
    const std::unordered_map<std::string, std::size_t>& chartIndexByFaceId,
    const std::vector<Image>& atlasImages,
    std::int32_t faceIndex,
    Vector3 point) {
    if (faceIndex < 0 || static_cast<std::size_t>(faceIndex) >= faces.size()) {
        return {0.0f, 0.0f, 0.0f};
    }
    const LightmapFace& face = faces[static_cast<std::size_t>(faceIndex)];
    const auto chartIt = chartIndexByFaceId.find(face.id);
    if (chartIt == chartIndexByFaceId.end() || chartIt->second >= rad.charts.size()) {
        return {0.0f, 0.0f, 0.0f};
    }
    const LightmapChart& chart = rad.charts[chartIt->second];
    if (chart.atlasIndex < 0 || static_cast<std::size_t>(chart.atlasIndex) >= atlasImages.size()) {
        return {0.0f, 0.0f, 0.0f};
    }

    Vector3 uAxis{};
    Vector3 vAxis{};
    axialUvAxes(face.normal, uAxis, vAxis);

    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
        const float u = dot3(face.vertices[i], uAxis);
        const float v = dot3(face.vertices[i], vAxis);
        if (i == 0) {
            uMin = uMax = u;
            vMin = vMax = v;
        } else {
            uMin = std::min(uMin, u);
            uMax = std::max(uMax, u);
            vMin = std::min(vMin, v);
            vMax = std::max(vMax, v);
        }
    }
    const float uSpan = uMax - uMin > 1e-5f ? uMax - uMin : 1.0f;
    const float vSpan = vMax - vMin > 1e-5f ? vMax - vMin : 1.0f;
    const float u = dot3(point, uAxis);
    const float v = dot3(point, vAxis);
    const float fu = std::clamp((u - uMin) / uSpan, 0.0f, 1.0f);
    const float fv = std::clamp((v - vMin) / vSpan, 0.0f, 1.0f);
    const float fx = chart.rotated ? fv : fu;
    const float fy = chart.rotated ? fu : fv;
    const float atlasU = chart.u0 + (chart.u1 - chart.u0) * fx;
    const float atlasV = chart.v0 + (chart.v1 - chart.v0) * fy;

    const Image& image = atlasImages[static_cast<std::size_t>(chart.atlasIndex)];
    if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
        return {0.0f, 0.0f, 0.0f};
    }
    const int x = std::clamp(static_cast<int>(std::lround(atlasU * static_cast<float>(image.width - 1))), 0, image.width - 1);
    const int y = std::clamp(static_cast<int>(std::lround(atlasV * static_cast<float>(image.height - 1))), 0, image.height - 1);
    const Color pixel = GetImageColor(image, x, y);

    LightmapEncoding encoding = LightmapEncoding::Ldr;
    if (static_cast<std::size_t>(chart.atlasIndex) < rad.atlases.size()) {
        encoding = rad.atlases[static_cast<std::size_t>(chart.atlasIndex)].encoding;
    }
    if (encoding == LightmapEncoding::Rgbe) {
        return decodeRgbe(pixel);
    }
    return {
        static_cast<float>(pixel.r) / 255.0f,
        static_cast<float>(pixel.g) / 255.0f,
        static_cast<float>(pixel.b) / 255.0f,
    };
}

} // namespace

std::vector<Vector3> placeCoarseLightProbes(const BspTree& tree, float cellSize) {
    std::vector<Vector3> positions;
    if (tree.root < 0) {
        return positions;
    }
    const CoarseGrid grid = buildCoarseGrid(tree, cellSize);
    for (int iz = 0; iz < grid.nz; ++iz) {
        for (int iy = 0; iy < grid.ny; ++iy) {
            for (int ix = 0; ix < grid.nx; ++ix) {
                if (!grid.isOpen(ix, iy, iz)) {
                    continue;
                }
                positions.push_back(nudgeAwayFromSolid(tree, grid.center(ix, iy, iz), grid.cellSize));
            }
        }
    }
    return positions;
}

std::vector<Vector3> placeFineLightProbes(const BspTree& tree, float cellSize, float fineCellSize) {
    std::vector<Vector3> positions;
    if (tree.root < 0) {
        return positions;
    }
    const CoarseGrid grid = buildCoarseGrid(tree, cellSize);
    const float fine = std::clamp(fineCellSize, 0.1f, grid.cellSize);
    const int subSteps = std::max(1, static_cast<int>(std::round(grid.cellSize / fine)));
    if (subSteps <= 1) {
        return positions;
    }

    for (int iz = 0; iz < grid.nz; ++iz) {
        for (int iy = 0; iy < grid.ny; ++iy) {
            for (int ix = 0; ix < grid.nx; ++ix) {
                if (!grid.isOpen(ix, iy, iz) || !isBoundaryCell(grid, ix, iy, iz)) {
                    continue;
                }
                const Vector3 center = grid.center(ix, iy, iz);
                const Vector3 cellMin = {
                    center.x - grid.cellSize * 0.5f,
                    center.y - grid.cellSize * 0.5f,
                    center.z - grid.cellSize * 0.5f,
                };
                for (int sz = 0; sz < subSteps; ++sz) {
                    for (int sy = 0; sy < subSteps; ++sy) {
                        for (int sx = 0; sx < subSteps; ++sx) {
                            const Vector3 sub = {
                                cellMin.x + (static_cast<float>(sx) + 0.5f) * fine,
                                cellMin.y + (static_cast<float>(sy) + 0.5f) * fine,
                                cellMin.z + (static_cast<float>(sz) + 0.5f) * fine,
                            };
                            if (leafIsOpenAt(tree, sub)) {
                                positions.push_back(nudgeAwayFromSolid(tree, sub, fine));
                            }
                        }
                    }
                }
            }
        }
    }
    return positions;
}

std::vector<LightProbe> bakeLightProbes(
    const std::vector<Vector3>& positions,
    float cellSize,
    const QuadBvh& sceneBvh,
    const std::vector<LightmapFace>& faces,
    const RadFile& rad,
    const std::vector<Image>& atlasImages,
    Vector3 ambientFallback,
    int sampleCount) {
    std::vector<LightProbe> result;
    result.reserve(positions.size());
    if (positions.empty() || cellSize <= 0.0f) {
        return result;
    }

    std::unordered_map<std::string, std::size_t> chartIndexByFaceId;
    chartIndexByFaceId.reserve(rad.charts.size());
    for (std::size_t i = 0; i < rad.charts.size(); ++i) {
        if (!rad.charts[i].faceId.empty()) {
            chartIndexByFaceId.emplace(rad.charts[i].faceId, i);
        }
    }

    sampleCount = std::max(sampleCount, 1);
    const float dcWeight = 1.0f / static_cast<float>(sampleCount);
    const float linearWeight = 3.0f / static_cast<float>(sampleCount);
    const float basisWeight[4] = {dcWeight, linearWeight, linearWeight, linearWeight};
    std::vector<Vector3> directions(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        directions[static_cast<std::size_t>(i)] = fibonacciSphereDirection(i, sampleCount);
    }

    constexpr float kMaxRayDistance = 10000.0f;
    for (const Vector3& position : positions) {
        Vector3 coeff[4] = {};
        for (const Vector3& dir : directions) {
            Vector3 radiance = ambientFallback;
            const auto hit = raycastBspSurfaces(sceneBvh, position, dir, kMaxRayDistance);
            if (hit) {
                radiance = sampleChartLinearRadiance(
                    faces,
                    rad,
                    chartIndexByFaceId,
                    atlasImages,
                    hit->faceIndex,
                    hit->point);
            }
            const float basis[4] = {1.0f, dir.x, dir.y, dir.z};
            for (int i = 0; i < 4; ++i) {
                coeff[i].x += radiance.x * basis[i] * basisWeight[i];
                coeff[i].y += radiance.y * basis[i] * basisWeight[i];
                coeff[i].z += radiance.z * basis[i] * basisWeight[i];
            }
        }

        LightProbe probe;
        probe.cellX = static_cast<std::int32_t>(std::lround(position.x / cellSize));
        probe.cellY = static_cast<std::int32_t>(std::lround(position.y / cellSize));
        probe.cellZ = static_cast<std::int32_t>(std::lround(position.z / cellSize));
        for (int i = 0; i < 4; ++i) {
            probe.shRgbe[static_cast<std::size_t>(i)] =
                encodeRgbe(std::max(coeff[i].x, 0.0f), std::max(coeff[i].y, 0.0f), std::max(coeff[i].z, 0.0f));
        }
        result.push_back(probe);
    }
    return result;
}

LightProbeBakeResult bakeLightProbeGrids(
    const BspTree& tree,
    const QuadBvh& sceneBvh,
    const std::vector<LightmapFace>& faces,
    const RadFile& rad,
    const std::vector<Image>& atlasImages,
    Vector3 ambientFallback,
    const LightProbeBakeSettings& settings) {
    LightProbeBakeResult result;
    result.coarse.cellSize = std::max(settings.cellSize, 0.25f);
    result.fine.cellSize = std::clamp(settings.fineCellSize, 0.1f, result.coarse.cellSize);

    const std::vector<Vector3> coarsePositions = placeCoarseLightProbes(tree, result.coarse.cellSize);
    const std::vector<Vector3> finePositions =
        placeFineLightProbes(tree, result.coarse.cellSize, result.fine.cellSize);

    result.coarse.probes = bakeLightProbes(
        coarsePositions,
        result.coarse.cellSize,
        sceneBvh,
        faces,
        rad,
        atlasImages,
        ambientFallback,
        settings.sampleCount);
    result.fine.probes = bakeLightProbes(
        finePositions,
        result.fine.cellSize,
        sceneBvh,
        faces,
        rad,
        atlasImages,
        ambientFallback,
        settings.sampleCount);

    TraceLog(
        LOG_INFO,
        "sloprad: light probes coarse=%d fine=%d cellSize=%.2f fineCellSize=%.2f",
        static_cast<int>(result.coarse.probes.size()),
        static_cast<int>(result.fine.probes.size()),
        result.coarse.cellSize,
        result.fine.cellSize);
    std::fflush(stdout);

    return result;
}

}
