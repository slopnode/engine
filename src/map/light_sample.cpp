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
    const float atlasU = chart.u0 + (chart.u1 - chart.u0) * fu;
    const float atlasV = chart.v0 + (chart.v1 - chart.v0) * fv;

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
        ProbeSH sh;
        for (int i = 0; i < 4; ++i) {
            sh.coeff[static_cast<std::size_t>(i)] = decodeRgbe(probe.shRgbe[static_cast<std::size_t>(i)]);
        }
        grid.probesByCell.emplace(ProbeCell{probe.cellX, probe.cellY, probe.cellZ}, sh);
    }
    return grid;
}

std::optional<Color> sampleProbeGrid(const ProbeGrid& grid, Vector3 point, Vector3 direction) {
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

    Vector3 coeff[4] = {};
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
                for (int i = 0; i < 4; ++i) {
                    coeff[i].x += it->second.coeff[i].x * weight;
                    coeff[i].y += it->second.coeff[i].y * weight;
                    coeff[i].z += it->second.coeff[i].z * weight;
                }
                totalWeight += weight;
            }
        }
    }

    if (totalWeight <= 1e-6f) {
        return std::nullopt;
    }
    const float invWeight = 1.0f / totalWeight;
    const float r = std::max(
        0.0f,
        (coeff[0].x + coeff[1].x * direction.x + coeff[2].x * direction.y + coeff[3].x * direction.z)
            * invWeight);
    const float g = std::max(
        0.0f,
        (coeff[0].y + coeff[1].y * direction.x + coeff[2].y * direction.y + coeff[3].y * direction.z)
            * invWeight);
    const float b = std::max(
        0.0f,
        (coeff[0].z + coeff[1].z * direction.x + coeff[2].z * direction.y + coeff[3].z * direction.z)
            * invWeight);
    return linearIrradianceToDisplayColor(r, g, b);
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
    const FacFile* fac,
    RadFile rad,
    std::vector<Image> atlasImages,
    Color ambient) {
    MapLighting lighting{};
    lighting.rad = std::move(rad);
    lighting.atlasImages = std::move(atlasImages);
    lighting.ambient = ambient;

    if (fac != nullptr && !fac->faces.empty()) {
        lighting.probeFaces = collectLightmapFaces(*fac);
    } else {
        lighting.probeFaces = probeFacesFromBsp(bsp);
    }
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

}
