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

Color sampleAtlasNearest(const Image& image, float u, float v) {
    if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
        return WHITE;
    }

    const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
    const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
    return GetImageColor(image, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
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
        lighting.atlasImages[static_cast<std::size_t>(chart.atlasIndex)],
        atlasU,
        atlasV);
    color.a = 255;
    return color;
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
    lighting.surfaceBvh = buildLightmapFaceBvh(lighting.probeFaces);

    for (std::size_t index = 0; index < lighting.rad.charts.size(); ++index) {
        const LightmapChart& chart = lighting.rad.charts[index];
        if (!chart.faceId.empty()) {
            lighting.chartIndexByFaceId.emplace(chart.faceId, index);
        }
    }

    lighting.available = !lighting.rad.charts.empty() && !lighting.atlasImages.empty()
        && !lighting.surfaceBvh.empty() && !lighting.probeFaces.empty();
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

}
