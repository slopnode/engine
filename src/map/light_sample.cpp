#include "map/light_sample.hpp"

#include "map/bsp_ray.hpp"
#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Color sampleAtlasNearest(
    const MapLighting& lighting,
    int atlasIndex,
    float u,
    float v) {
    if (atlasIndex < 0 || atlasIndex >= static_cast<int>(lighting.atlasImages.size())) {
        return WHITE;
    }
    const Image& image = lighting.atlasImages[static_cast<std::size_t>(atlasIndex)];
    if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
        return WHITE;
    }

    const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
    const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
    const Color pixel = GetImageColor(image, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));

    LightmapEncoding encoding = LightmapEncoding::Ldr;
    if (atlasIndex >= 0 && static_cast<std::size_t>(atlasIndex) < lighting.rad.atlases.size()) {
        encoding = lighting.rad.atlases[static_cast<std::size_t>(atlasIndex)].encoding;
    }
    if (encoding == LightmapEncoding::Rgbe) {
        const Vector3 linear = decodeRgbe(pixel);
        return linearIrradianceToDisplayColor(linear.x, linear.y, linear.z);
    }
    Color out = pixel;
    out.a = 255;
    return out;
}

std::optional<Color> sampleChartAtPoint(
    const MapLighting& lighting,
    const LightmapFace& face,
    const LightmapChart& chart,
    Vector3 point) {
    if (face.vertices.size() < 3) {
        return std::nullopt;
    }
    if (chart.atlasIndex < 0 ||
        chart.atlasIndex >= static_cast<std::int32_t>(lighting.atlasImages.size())) {
        return std::nullopt;
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

    Color color = sampleAtlasNearest(
        lighting,
        chart.atlasIndex,
        atlasU,
        atlasV);
    return color;
}

ProbeGrid decodeProbeGrid(const LightProbeGridInfo& info) {
    ProbeGrid grid;
    grid.cellSize = info.cellSize;
    grid.probesByCell.reserve(info.probes.size());
    for (const LightProbe& probe : info.probes) {
        ProbeCube cube;
        for (std::size_t i = 0; i < kProbeCubeFaces; ++i) {
            cube.face[i] = decodeRgbe(probe.cubeRgbe[i]);
        }
        grid.probesByCell.emplace(ProbeCell{probe.cellX, probe.cellY, probe.cellZ}, cube);
    }
    return grid;
}

std::optional<ProbeCube> interpolateProbeGrid(const ProbeGrid& grid, Vector3 point) {
    if (grid.probesByCell.empty() || grid.cellSize <= 0.0f) {
        return std::nullopt;
    }

    const float gx = point.x / grid.cellSize;
    const float gy = point.y / grid.cellSize;
    const float gz = point.z / grid.cellSize;
    const std::int32_t ix0 = static_cast<std::int32_t>(std::floor(gx));
    const std::int32_t iy0 = static_cast<std::int32_t>(std::floor(gy));
    const std::int32_t iz0 = static_cast<std::int32_t>(std::floor(gz));
    const float fx = gx - static_cast<float>(ix0);
    const float fy = gy - static_cast<float>(iy0);
    const float fz = gz - static_cast<float>(iz0);

    Vector3 faces[kProbeCubeFaces] = {};
    float totalWeight = 0.0f;
    for (int dz = 0; dz <= 1; ++dz) {
        const float wz = dz == 0 ? (1.0f - fz) : fz;
        for (int dy = 0; dy <= 1; ++dy) {
            const float wy = dy == 0 ? (1.0f - fy) : fy;
            for (int dx = 0; dx <= 1; ++dx) {
                const float wx = dx == 0 ? (1.0f - fx) : fx;
                const float weight = wx * wy * wz;
                if (weight <= 0.0f) {
                    continue;
                }
                const ProbeCell cell{ix0 + dx, iy0 + dy, iz0 + dz};
                const auto it = grid.probesByCell.find(cell);
                if (it == grid.probesByCell.end()) {
                    continue;
                }
                for (std::size_t i = 0; i < kProbeCubeFaces; ++i) {
                    faces[i].x += it->second.face[i].x * weight;
                    faces[i].y += it->second.face[i].y * weight;
                    faces[i].z += it->second.face[i].z * weight;
                }
                totalWeight += weight;
            }
        }
    }

    if (totalWeight <= 1e-6f) {
        return std::nullopt;
    }
    const float invWeight = 1.0f / totalWeight;
    ProbeCube result;
    for (std::size_t i = 0; i < kProbeCubeFaces; ++i) {
        result.face[i] = {faces[i].x * invWeight, faces[i].y * invWeight, faces[i].z * invWeight};
    }
    return result;
}

std::optional<Color> sampleProbeGrid(const ProbeGrid& grid, Vector3 point, Vector3 direction) {
    const auto cube = interpolateProbeGrid(grid, point);
    if (!cube) {
        return std::nullopt;
    }
    const Vector3 irradiance = evaluateProbeCube(*cube, direction);
    return linearIrradianceToDisplayColor(irradiance.x, irradiance.y, irradiance.z);
}

std::vector<LightmapFace> probeFacesFromBsp(const BspTree& bsp) {
    std::vector<LightmapFace> faces;
    faces.reserve(bsp.surfaceFaces.size());
    for (const BspSurfaceFace& surface : bsp.surfaceFaces) {
        if (surface.vertices.size() < 3) {
            continue;
        }
        LightmapFace face;
        face.id = surface.id;
        face.material = surface.material;
        face.normal = surface.normal;
        face.vertices = surface.vertices;
        face.uvShiftPixels = surface.uvShiftPixels;
        faces.push_back(std::move(face));
    }
    return faces;
}

} // namespace

MapLighting buildMapLighting(
    const BspTree& bsp,
    RadFile rad,
    std::vector<Image> atlasImages,
    Color ambient) {
    MapLighting lighting{};
    lighting.rad = std::move(rad);
    lighting.atlasImages = std::move(atlasImages);
    lighting.ambient = ambient;

    lighting.probeFaces = probeFacesFromBsp(bsp);
    lighting.faceTransparentSkip.assign(lighting.probeFaces.size(), 0);
    for (std::size_t i = 0; i < lighting.probeFaces.size(); ++i) {
        if (lighting.probeFaces[i].transparent) {
            lighting.faceTransparentSkip[i] = 1;
        }
    }
    lighting.surfaceBvh = buildLightmapFaceBvh(lighting.probeFaces);

    for (std::size_t index = 0; index < lighting.rad.charts.size(); ++index) {
        const LightmapChart& chart = lighting.rad.charts[index];
        if (!chart.faceId.empty()) {
            lighting.chartIndexByFaceId.emplace(chart.faceId, index);
        }
    }

    lighting.available = !lighting.rad.charts.empty() && !lighting.atlasImages.empty()
        && !lighting.surfaceBvh.empty() && !lighting.probeFaces.empty();

    lighting.probeGridFine = decodeProbeGrid(lighting.rad.probeGridFine);
    lighting.probeGridCoarse = decodeProbeGrid(lighting.rad.probeGridCoarse);

    return lighting;
}

std::optional<Color> sampleMapLight(
    const MapLighting& lighting,
    Vector3 origin,
    Vector3 direction,
    float maxDistance) {
    if (!lighting.available) {
        return std::nullopt;
    }

    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (dirLenSq < 1e-12f) {
        return std::nullopt;
    }

    const auto hit = raycastBspSurfaces(lighting.surfaceBvh, origin, direction, maxDistance);
    if (!hit) {
        return std::nullopt;
    }
    if (hit->faceIndex < 0 ||
        hit->faceIndex >= static_cast<std::int32_t>(lighting.probeFaces.size())) {
        return std::nullopt;
    }

    const LightmapFace& face = lighting.probeFaces[static_cast<std::size_t>(hit->faceIndex)];
    const auto chartIt = lighting.chartIndexByFaceId.find(face.id);
    if (chartIt == lighting.chartIndexByFaceId.end()) {
        return std::nullopt;
    }
    if (chartIt->second >= lighting.rad.charts.size()) {
        return std::nullopt;
    }

    return sampleChartAtPoint(
        lighting,
        face,
        lighting.rad.charts[chartIt->second],
        hit->point);
}

std::optional<Color> sampleLightProbe(
    const MapLighting& lighting,
    Vector3 point,
    Vector3 direction) {
    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (dirLenSq < 1e-12f) {
        return std::nullopt;
    }
    const float invDirLen = 1.0f / std::sqrt(dirLenSq);
    const Vector3 dir = {direction.x * invDirLen, direction.y * invDirLen, direction.z * invDirLen};

    if (auto fine = sampleProbeGrid(lighting.probeGridFine, point, dir)) {
        return fine;
    }
    return sampleProbeGrid(lighting.probeGridCoarse, point, dir);
}

std::optional<ProbeCube> sampleLightProbeCube(const MapLighting& lighting, Vector3 point) {
    if (auto fine = interpolateProbeGrid(lighting.probeGridFine, point)) {
        return fine;
    }
    return interpolateProbeGrid(lighting.probeGridCoarse, point);
}

Vector3 evaluateProbeCube(const ProbeCube& cube, Vector3 normal) {
    const float axis[3] = {normal.x, normal.y, normal.z};
    Vector3 result{};
    for (int i = 0; i < 3; ++i) {
        const float weight = axis[i] * axis[i];
        const Vector3& face = cube.face[i * 2 + (axis[i] >= 0.0f ? 0 : 1)];
        result.x += face.x * weight;
        result.y += face.y * weight;
        result.z += face.z * weight;
    }
    return result;
}

}
