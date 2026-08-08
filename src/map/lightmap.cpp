#include "map/lightmap.hpp"

#include "map/uv_math.hpp"

#include <rlgl.h>
#include "external/glad.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace slopengine {

namespace {

class BinaryWriter {
public:
    void writeBytes(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::byte*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }

    template <typename T>
    void writePod(const T& value) {
        writeBytes(&value, sizeof(T));
    }

    void writeString(const std::string& value) {
        const std::uint32_t length = static_cast<std::uint32_t>(value.size());
        writePod(length);
        if (length > 0) {
            writeBytes(value.data(), length);
        }
    }

    const std::vector<std::byte>& buffer() const { return buffer_; }

private:
    std::vector<std::byte> buffer_;
};

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> data)
        : data_(data) {}

    bool readBytes(void* out, std::size_t size) {
        if (cursor_ + size > data_.size()) {
            return false;
        }
        std::memcpy(out, data_.data() + cursor_, size);
        cursor_ += size;
        return true;
    }

    template <typename T>
    bool readPod(T& value) {
        return readBytes(&value, sizeof(T));
    }

    bool readString(std::string& value) {
        std::uint32_t length = 0;
        if (!readPod(length)) {
            return false;
        }
        if (cursor_ + length > data_.size()) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(data_.data() + cursor_),
            static_cast<std::size_t>(length));
        cursor_ += length;
        return true;
    }

private:
    std::span<const std::byte> data_;
    std::size_t cursor_ = 0;
};

constexpr float kGroupPlaneEps = 1e-4f;
constexpr float kGroupNormalCosThreshold = 0.999f;
constexpr float kGroupAdjacencyEps = 1e-3f;

float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

bool sameLightmapUvFrame(const LightmapFace& a, const LightmapFace& b) {
    if (a.material != b.material || a.uvLock != b.uvLock || a.transparent != b.transparent) {
        return false;
    }
    if (std::fabs(a.uvShiftPixels.x - b.uvShiftPixels.x) > 1e-3f
        || std::fabs(a.uvShiftPixels.y - b.uvShiftPixels.y) > 1e-3f) {
        return false;
    }
    if (std::fabs(a.uvScale.x - b.uvScale.x) > 1e-3f || std::fabs(a.uvScale.y - b.uvScale.y) > 1e-3f) {
        return false;
    }
    if (dot3(a.normal, b.normal) < kGroupNormalCosThreshold) {
        return false;
    }
    if (a.uvLock
        && (dot3(a.uvUAxis, b.uvUAxis) < kGroupNormalCosThreshold
            || dot3(a.uvVAxis, b.uvVAxis) < kGroupNormalCosThreshold)) {
        return false;
    }
    return true;
}

bool facesCoplanar(const LightmapFace& a, const LightmapFace& b) {
    if (a.vertices.empty() || b.vertices.empty()) {
        return false;
    }
    const float planeA = dot3(a.normal, a.vertices[0]);
    const float planeB = dot3(a.normal, b.vertices[0]);
    return std::fabs(planeA - planeB) <= kGroupPlaneEps;
}

Vec2 projectToUv(Vector3 v, Vector3 uAxis, Vector3 vAxis) {
    return {dot3(v, uAxis), dot3(v, vAxis)};
}

float pointSegmentDist2d(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const float abLenSq = ab.x * ab.x + ab.y * ab.y;
    if (abLenSq <= 1e-12f) {
        const float dx = p.x - a.x;
        const float dy = p.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / abLenSq, 0.0f, 1.0f);
    const float px = a.x + ab.x * t;
    const float py = a.y + ab.y * t;
    const float dx = p.x - px;
    const float dy = p.y - py;
    return std::sqrt(dx * dx + dy * dy);
}

bool facesAdjacent(const LightmapFace& a, const LightmapFace& b, Vector3 uAxis, Vector3 vAxis) {
    std::vector<Vec2> pa;
    pa.reserve(a.vertices.size());
    for (const Vector3& v : a.vertices) {
        pa.push_back(projectToUv(v, uAxis, vAxis));
    }
    std::vector<Vec2> pb;
    pb.reserve(b.vertices.size());
    for (const Vector3& v : b.vertices) {
        pb.push_back(projectToUv(v, uAxis, vAxis));
    }
    for (std::size_t i = 0; i < pa.size(); ++i) {
        const Vec2 a0 = pa[i];
        const Vec2 a1 = pa[(i + 1) % pa.size()];
        for (std::size_t j = 0; j < pb.size(); ++j) {
            const Vec2 b0 = pb[j];
            const Vec2 b1 = pb[(j + 1) % pb.size()];
            if (pointSegmentDist2d(a0, b0, b1) <= kGroupAdjacencyEps
                || pointSegmentDist2d(a1, b0, b1) <= kGroupAdjacencyEps
                || pointSegmentDist2d(b0, a0, a1) <= kGroupAdjacencyEps
                || pointSegmentDist2d(b1, a0, a1) <= kGroupAdjacencyEps) {
                return true;
            }
        }
    }
    return false;
}

std::int32_t findGroupRoot(std::vector<std::int32_t>& parent, std::int32_t x) {
    while (parent[static_cast<std::size_t>(x)] != x) {
        parent[static_cast<std::size_t>(x)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
        x = parent[static_cast<std::size_t>(x)];
    }
    return x;
}

} // namespace

std::vector<LightmapFace> collectLightmapFaces(const std::vector<Brush>& brushes) {
    std::vector<LightmapFace> faces;
    for (const Brush& brush : brushes) {
        for (const BrushFace& brushFace : brush.faces) {
            if (brushFace.nodraw) {
                continue;
            }
            LightmapFace face;
            face.id = brushFace.id;
            face.material = brushFace.material;
            face.normal = brushFace.normal;
            face.vertices = brushFace.vertices;
            face.uvShiftPixels = brushFace.uvShiftPixels;
            face.uvScale = brushFace.uvScale;
            face.uvUAxis = brushFace.uvUAxis;
            face.uvVAxis = brushFace.uvVAxis;
            face.uvLock = brushFace.uvLock;
            face.transparent = brush.role == BrushRole::Transparent;
            faces.push_back(std::move(face));
        }
    }
    return faces;
}

std::vector<LightmapFace> collectLightmapFaces(const FacFile& vis) {
    std::vector<LightmapFace> faces;
    faces.reserve(vis.faces.size());
    for (const VisibleFace& visible : vis.faces) {
        if (visible.vertices.size() < 3) {
            continue;
        }
        LightmapFace face;
        face.id = visible.id;
        face.material = visible.material;
        face.normal = visible.normal;
        face.vertices = visible.vertices;
        face.uvShiftPixels = visible.uvShiftPixels;
        face.uvScale = visible.uvScale;
        face.uvUAxis = visible.uvUAxis;
        face.uvVAxis = visible.uvVAxis;
        face.uvLock = visible.uvLock;
        face.interiorLeaf = visible.interiorLeaf;
        face.transparent = visible.transparent;
        faces.push_back(std::move(face));
    }
    return faces;
}

std::vector<LightmapFaceGroup> groupCoplanarLightmapFaces(const std::vector<LightmapFace>& faces) {
    const std::size_t n = faces.size();
    std::vector<std::int32_t> parent(n);
    for (std::size_t i = 0; i < n; ++i) {
        parent[i] = static_cast<std::int32_t>(i);
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (faces[i].vertices.size() < 3) {
            continue;
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (faces[j].vertices.size() < 3) {
                continue;
            }
            if (findGroupRoot(parent, static_cast<std::int32_t>(i))
                == findGroupRoot(parent, static_cast<std::int32_t>(j))) {
                continue;
            }
            if (!sameLightmapUvFrame(faces[i], faces[j]) || !facesCoplanar(faces[i], faces[j])) {
                continue;
            }
            Vector3 uAxis{};
            Vector3 vAxis{};
            faceUvAxes(
                faces[i].uvLock,
                faces[i].normal,
                faces[i].uvUAxis,
                faces[i].uvVAxis,
                uAxis,
                vAxis);
            if (!facesAdjacent(faces[i], faces[j], uAxis, vAxis)) {
                continue;
            }
            const std::int32_t rootI = findGroupRoot(parent, static_cast<std::int32_t>(i));
            const std::int32_t rootJ = findGroupRoot(parent, static_cast<std::int32_t>(j));
            parent[static_cast<std::size_t>(rootI)] = rootJ;
        }
    }

    std::unordered_map<std::int32_t, std::vector<std::int32_t>> buckets;
    for (std::size_t i = 0; i < n; ++i) {
        if (faces[i].vertices.size() < 3) {
            continue;
        }
        buckets[findGroupRoot(parent, static_cast<std::int32_t>(i))].push_back(static_cast<std::int32_t>(i));
    }

    std::vector<LightmapFaceGroup> groups;
    groups.reserve(buckets.size());
    for (auto& [root, members] : buckets) {
        LightmapFaceGroup group;
        group.faceIndices = members;
        const LightmapFace& representative = faces[static_cast<std::size_t>(members.front())];
        faceUvAxes(
            representative.uvLock,
            representative.normal,
            representative.uvUAxis,
            representative.uvVAxis,
            group.uAxis,
            group.vAxis);
        bool first = true;
        for (std::int32_t index : members) {
            for (const Vector3& vertex : faces[static_cast<std::size_t>(index)].vertices) {
                const float u = dot3(vertex, group.uAxis);
                const float v = dot3(vertex, group.vAxis);
                if (first) {
                    group.uMin = group.uMax = u;
                    group.vMin = group.vMax = v;
                    first = false;
                } else {
                    group.uMin = std::min(group.uMin, u);
                    group.uMax = std::max(group.uMax, u);
                    group.vMin = std::min(group.vMin, v);
                    group.vMax = std::max(group.vMax, v);
                }
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

LightmapPackResult packLightmapCharts(
    const std::vector<LightmapFace>& faces,
    float luxelsPerMeter,
    int atlasSize,
    const std::vector<char>* skipFaces) {
    LightmapPackResult result;
    result.rad.luxelsPerMeter = luxelsPerMeter;
    atlasSize = std::max(2, atlasSize);

    auto isSkipped = [&](std::int32_t faceIndex) {
        return skipFaces != nullptr && static_cast<std::size_t>(faceIndex) < skipFaces->size()
            && (*skipFaces)[static_cast<std::size_t>(faceIndex)] != 0;
    };

    const std::vector<LightmapFaceGroup> allGroups = groupCoplanarLightmapFaces(faces);

    struct PendingChart {
        LightmapFaceGroup group;
        int luxelW = 0;
        int luxelH = 0;
    };
    std::vector<PendingChart> pending;
    pending.reserve(allGroups.size());
    constexpr float kLargeFaceMeters = 4.0f;
    for (const LightmapFaceGroup& group : allGroups) {
        LightmapFaceGroup filtered;
        filtered.uAxis = group.uAxis;
        filtered.vAxis = group.vAxis;
        bool first = true;
        for (std::int32_t faceIndex : group.faceIndices) {
            if (isSkipped(faceIndex)) {
                continue;
            }
            filtered.faceIndices.push_back(faceIndex);
            for (const Vector3& vertex : faces[static_cast<std::size_t>(faceIndex)].vertices) {
                const float u = filtered.uAxis.x * vertex.x + filtered.uAxis.y * vertex.y
                    + filtered.uAxis.z * vertex.z;
                const float v = filtered.vAxis.x * vertex.x + filtered.vAxis.y * vertex.y
                    + filtered.vAxis.z * vertex.z;
                if (first) {
                    filtered.uMin = filtered.uMax = u;
                    filtered.vMin = filtered.vMax = v;
                    first = false;
                } else {
                    filtered.uMin = std::min(filtered.uMin, u);
                    filtered.uMax = std::max(filtered.uMax, u);
                    filtered.vMin = std::min(filtered.vMin, v);
                    filtered.vMax = std::max(filtered.vMax, v);
                }
            }
        }
        if (filtered.faceIndices.empty()) {
            continue;
        }

        const float widthMeters = std::max(0.01f, filtered.uMax - filtered.uMin);
        const float heightMeters = std::max(0.01f, filtered.vMax - filtered.vMin);
        float effectiveLpm = luxelsPerMeter;
        if (std::max(widthMeters, heightMeters) > kLargeFaceMeters) {
            effectiveLpm *= 0.5f;
        }
        PendingChart chart;
        chart.luxelW = std::clamp(
            static_cast<int>(std::ceil(widthMeters * effectiveLpm)) + 2,
            2,
            atlasSize);
        chart.luxelH = std::clamp(
            static_cast<int>(std::ceil(heightMeters * effectiveLpm)) + 2,
            2,
            atlasSize);
        chart.group = std::move(filtered);
        pending.push_back(std::move(chart));
    }
    std::sort(pending.begin(), pending.end(), [](const PendingChart& a, const PendingChart& b) {
        if (a.luxelH != b.luxelH) {
            return a.luxelH > b.luxelH;
        }
        if (a.luxelW != b.luxelW) {
            return a.luxelW > b.luxelW;
        }
        return a.group.faceIndices.front() < b.group.faceIndices.front();
    });

    int atlasIndex = 0;
    int cursorX = 0;
    int cursorY = 0;
    int rowHeight = 0;

    auto ensureAtlas = [&]() {
        while (static_cast<int>(result.rad.atlases.size()) <= atlasIndex) {
            LightmapAtlasInfo info;
            info.width = atlasSize;
            info.height = atlasSize;
            info.texturePath = "atlas" + std::to_string(static_cast<int>(result.rad.atlases.size()));
            result.rad.atlases.push_back(info);
            result.atlasRgb.emplace_back(
                static_cast<std::size_t>(atlasSize * atlasSize * 3),
                0.0f);
        }
    };

    auto newAtlas = [&]() {
        ++atlasIndex;
        cursorX = 0;
        cursorY = 0;
        rowHeight = 0;
        ensureAtlas();
    };

    ensureAtlas();

    for (PendingChart& pendingChart : pending) {
        const int luxelW = pendingChart.luxelW;
        const int luxelH = pendingChart.luxelH;
        if (cursorX + luxelW > atlasSize) {
            cursorX = 0;
            cursorY += rowHeight;
            rowHeight = 0;
        }
        if (cursorY + luxelH > atlasSize) {
            newAtlas();
        }

        const float u0 = (static_cast<float>(cursorX) + 0.5f) / static_cast<float>(atlasSize);
        const float v0 = (static_cast<float>(cursorY) + 0.5f) / static_cast<float>(atlasSize);
        const float u1 = (static_cast<float>(cursorX + luxelW) - 0.5f) / static_cast<float>(atlasSize);
        const float v1 = (static_cast<float>(cursorY + luxelH) - 0.5f) / static_cast<float>(atlasSize);

        for (std::int32_t faceIndex : pendingChart.group.faceIndices) {
            const LightmapFace& face = faces[static_cast<std::size_t>(faceIndex)];
            LightmapChart chart;
            chart.faceIndex = faceIndex;
            chart.faceId = face.id;
            chart.atlasIndex = atlasIndex;
            chart.luxelWidth = luxelW;
            chart.luxelHeight = luxelH;
            chart.atlasX = cursorX;
            chart.atlasY = cursorY;
            chart.u0 = u0;
            chart.v0 = v0;
            chart.u1 = u1;
            chart.v1 = v1;
            chart.groupUMin = pendingChart.group.uMin;
            chart.groupUMax = pendingChart.group.uMax;
            chart.groupVMin = pendingChart.group.vMin;
            chart.groupVMax = pendingChart.group.vMax;
            result.rad.charts.push_back(std::move(chart));
        }

        pendingChart.group.atlasIndex = atlasIndex;
        pendingChart.group.atlasX = cursorX;
        pendingChart.group.atlasY = cursorY;
        pendingChart.group.luxelWidth = luxelW;
        pendingChart.group.luxelHeight = luxelH;
        result.groups.push_back(std::move(pendingChart.group));

        cursorX += luxelW;
        rowHeight = std::max(rowHeight, luxelH);
    }

    if (result.rad.atlases.empty()) {
        ensureAtlas();
    }

    return result;
}

Color encodeRgbe(float r, float g, float b) {
    const float maxc = std::max({r, g, b, 0.0f});
    if (maxc < 1e-32f) {
        return {0, 0, 0, 0};
    }
    const int exponent = static_cast<int>(std::floor(std::log2(maxc))) + 1;
    const float scale = std::ldexp(256.0f, -exponent);
    Color out;
    out.r = static_cast<unsigned char>(std::clamp(r * scale, 0.0f, 255.0f));
    out.g = static_cast<unsigned char>(std::clamp(g * scale, 0.0f, 255.0f));
    out.b = static_cast<unsigned char>(std::clamp(b * scale, 0.0f, 255.0f));
    out.a = static_cast<unsigned char>(exponent + 128);
    return out;
}

Vector3 decodeRgbe(Color pixel) {
    if (pixel.a == 0) {
        return {0.0f, 0.0f, 0.0f};
    }
    const float scale = std::ldexp(1.0f / 256.0f, static_cast<int>(pixel.a) - 128);
    return {
        static_cast<float>(pixel.r) * scale,
        static_cast<float>(pixel.g) * scale,
        static_cast<float>(pixel.b) * scale,
    };
}

Color linearIrradianceToDisplayColor(float r, float g, float b) {
    const auto tonemap = [](float value) {
        return static_cast<unsigned char>(
            std::clamp(value / (1.0f + value) * 255.0f, 0.0f, 255.0f));
    };
    return {tonemap(r), tonemap(g), tonemap(b), 255};
}

LightmapEncoding primaryLightmapEncoding(const RadFile& rad) {
    for (const LightmapAtlasInfo& atlas : rad.atlases) {
        if (atlas.encoding == LightmapEncoding::Rgbe) {
            return LightmapEncoding::Rgbe;
        }
    }
    return LightmapEncoding::Ldr;
}

bool writeRadFile(const std::filesystem::path& path, const RadFile& rad) {
    BinaryWriter writer;
    writer.writePod(kRadMagic);
    writer.writePod(kRadVersion);
    writer.writePod(rad.luxelsPerMeter);
    writer.writePod(static_cast<std::uint32_t>(rad.atlases.size()));
    for (const LightmapAtlasInfo& atlas : rad.atlases) {
        writer.writeString(atlas.texturePath);
        writer.writePod(atlas.width);
        writer.writePod(atlas.height);
        writer.writePod(static_cast<std::uint32_t>(atlas.encoding));
    }
    writer.writePod(static_cast<std::uint32_t>(rad.charts.size()));
    for (const LightmapChart& chart : rad.charts) {
        writer.writePod(chart.faceIndex);
        writer.writeString(chart.faceId);
        writer.writePod(chart.atlasIndex);
        writer.writePod(chart.luxelWidth);
        writer.writePod(chart.luxelHeight);
        writer.writePod(chart.atlasX);
        writer.writePod(chart.atlasY);
        writer.writePod(chart.u0);
        writer.writePod(chart.v0);
        writer.writePod(chart.u1);
        writer.writePod(chart.v1);
        writer.writePod(chart.groupUMin);
        writer.writePod(chart.groupUMax);
        writer.writePod(chart.groupVMin);
        writer.writePod(chart.groupVMax);
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.write(
        reinterpret_cast<const char*>(writer.buffer().data()),
        static_cast<std::streamsize>(writer.buffer().size()));
    return static_cast<bool>(file);
}

std::optional<RadFile> readRadBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version)) {
        return std::nullopt;
    }
    if (magic != kRadMagic
        || (version != kRadVersionLegacy && version != kRadVersionPrevious && version != kRadVersion)) {
        return std::nullopt;
    }

    RadFile rad;
    if (!reader.readPod(rad.luxelsPerMeter)) {
        return std::nullopt;
    }

    std::uint32_t atlasCount = 0;
    if (!reader.readPod(atlasCount)) {
        return std::nullopt;
    }
    rad.atlases.resize(atlasCount);
    for (LightmapAtlasInfo& atlas : rad.atlases) {
        if (!reader.readString(atlas.texturePath) || !reader.readPod(atlas.width)
            || !reader.readPod(atlas.height)) {
            return std::nullopt;
        }
        if (version >= kRadVersionPrevious) {
            std::uint32_t encoding = 0;
            if (!reader.readPod(encoding)) {
                return std::nullopt;
            }
            atlas.encoding = encoding == static_cast<std::uint32_t>(LightmapEncoding::Rgbe)
                ? LightmapEncoding::Rgbe
                : LightmapEncoding::Ldr;
        } else {
            atlas.encoding = LightmapEncoding::Ldr;
        }
    }

    std::uint32_t chartCount = 0;
    if (!reader.readPod(chartCount)) {
        return std::nullopt;
    }
    rad.charts.resize(chartCount);
    for (LightmapChart& chart : rad.charts) {
        if (!reader.readPod(chart.faceIndex) || !reader.readString(chart.faceId)
            || !reader.readPod(chart.atlasIndex) || !reader.readPod(chart.luxelWidth)
            || !reader.readPod(chart.luxelHeight) || !reader.readPod(chart.atlasX)
            || !reader.readPod(chart.atlasY) || !reader.readPod(chart.u0) || !reader.readPod(chart.v0)
            || !reader.readPod(chart.u1) || !reader.readPod(chart.v1)) {
            return std::nullopt;
        }
        if (version >= kRadVersion) {
            if (!reader.readPod(chart.groupUMin) || !reader.readPod(chart.groupUMax)
                || !reader.readPod(chart.groupVMin) || !reader.readPod(chart.groupVMax)) {
                return std::nullopt;
            }
        }
    }
    return rad;
}

std::optional<RadFile> readRadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const auto size = file.tellg();
    if (size <= 0) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file) {
        return std::nullopt;
    }
    return readRadBytes(buffer);
}

Shader loadLightmapShader(AssetStore& assets, int& useLightmapLoc) {
    useLightmapLoc = -1;
    const std::string vert = assets.getShaderSource("default/lightmap_vert");
    const std::string frag = assets.getShaderSource("default/lightmap_frag");
    if (vert.empty() || frag.empty()) {
        TraceLog(LOG_WARNING, "MAP: missing lightmap shaders");
        return {};
    }
    Shader shader = LoadShaderFromMemory(vert.c_str(), frag.c_str());
    if (shader.id == 0) {
        TraceLog(LOG_WARNING, "MAP: failed to compile lightmap shaders");
        return {};
    }
    shader.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(shader, "texture1");
    shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, "texture5");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_COLOR_SPECULAR] = GetShaderLocation(shader, "colSpecular");
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] < 0) {
        shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    }
    shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader, "matNormal");
    useLightmapLoc = GetShaderLocation(shader, "useLightmap");
    int useLightmap = 1;
    if (useLightmapLoc >= 0) {
        SetShaderValue(shader, useLightmapLoc, &useLightmap, SHADER_UNIFORM_INT);
    }
    const int solidLitLoc = GetShaderLocation(shader, "solidLit");
    if (solidLitLoc >= 0) {
        const int solidLit = 0;
        SetShaderValue(shader, solidLitLoc, &solidLit, SHADER_UNIFORM_INT);
    }
    const int lightCountLoc = GetShaderLocation(shader, "dynLightCount");
    if (lightCountLoc >= 0) {
        const int zero = 0;
        SetShaderValue(shader, lightCountLoc, &zero, SHADER_UNIFORM_INT);
    }
    applyLightmapEncoding(shader, LightmapEncoding::Ldr);
    bindLightmapDummyShadowMaps(shader);
    return shader;
}

void applyLightmapEncoding(Shader shader, LightmapEncoding encoding) {
    if (shader.id == 0) {
        return;
    }
    const int loc = GetShaderLocation(shader, "lightmapEncoding");
    if (loc < 0) {
        return;
    }
    const int value = encoding == LightmapEncoding::Rgbe ? 1 : 0;
    SetShaderValue(shader, loc, &value, SHADER_UNIFORM_INT);
}

namespace {

std::string stripLeadingVersionDirective(std::string source) {
    if (source.rfind("#version", 0) == 0) {
        const std::size_t end = source.find('\n');
        if (end != std::string::npos) {
            source.erase(0, end + 1);
        }
    }
    return source;
}

std::string buildSkyFragmentSource(AssetStore& assets, const char* fragPath) {
    std::string frag = assets.getShaderSource(fragPath);
    std::string sample = stripLeadingVersionDirective(assets.getShaderSource("default/sky_sample"));
    const std::string includeToken = "#include \"SKY_SAMPLE\"";
    const std::size_t includePos = frag.find(includeToken);
    if (includePos != std::string::npos) {
        frag.replace(includePos, includeToken.size(), sample);
    }
    return frag;
}

Shader loadSkyShader(AssetStore& assets, const char* vertPath, const char* fragPath) {
    const std::string vert = assets.getShaderSource(vertPath);
    const std::string frag = buildSkyFragmentSource(assets, fragPath);
    if (vert.empty() || frag.empty()) {
        return {};
    }
    return LoadShaderFromMemory(vert.c_str(), frag.c_str());
}

} // namespace

Shader loadSkyboxBackgroundShader(AssetStore& assets) {
    return loadSkyShader(assets, "default/skybox_vert", "default/skybox_frag");
}

Shader loadSkyFaceShader(AssetStore& assets) {
    return loadSkyShader(assets, "default/sky_face_vert", "default/sky_face_frag");
}

void bindLightmapDummyShadowMaps(Shader shader) {
    if (shader.id == 0) {
        return;
    }
    const int loc = GetShaderLocation(shader, "dynShadowMaps");
    if (loc < 0) {
        return;
    }

    static unsigned int dummyArrayId = 0;
    if (dummyArrayId == 0) {
        const unsigned char pixel = 255;
        glGenTextures(1, &dummyArrayId);
        glBindTexture(GL_TEXTURE_2D_ARRAY, dummyArrayId);
        glTexImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            GL_R8,
            1,
            1,
            1,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            &pixel);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    const int unit = 12;
    rlDrawRenderBatchActive();
    rlActiveTextureSlot(unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, dummyArrayId);
    SetShaderValue(shader, loc, &unit, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(0);
}

}
