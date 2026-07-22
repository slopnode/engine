#include "map/lightmap.hpp"

#include "map/uv_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

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

float faceExtent(const LightmapFace& face, Vector3 axis) {
    float minV = 0.0f;
    float maxV = 0.0f;
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
        const float value =
            face.vertices[i].x * axis.x + face.vertices[i].y * axis.y + face.vertices[i].z * axis.z;
        if (i == 0) {
            minV = maxV = value;
        } else {
            minV = std::min(minV, value);
            maxV = std::max(maxV, value);
        }
    }
    return std::max(0.01f, maxV - minV);
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
            face.uvUAxis = brushFace.uvUAxis;
            face.uvVAxis = brushFace.uvVAxis;
            face.uvLock = brushFace.uvLock;
            faces.push_back(std::move(face));
        }
    }
    return faces;
}

std::vector<LightmapFace> collectLightmapFaces(const VisFile& vis) {
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
        face.uvUAxis = visible.uvUAxis;
        face.uvVAxis = visible.uvVAxis;
        face.uvLock = visible.uvLock;
        face.interiorLeaf = visible.interiorLeaf;
        faces.push_back(std::move(face));
    }
    return faces;
}

LightmapPackResult packLightmapCharts(
    const std::vector<LightmapFace>& faces,
    float luxelsPerMeter,
    int atlasSize) {
    LightmapPackResult result;
    result.rad.luxelsPerMeter = luxelsPerMeter;
    atlasSize = std::max(2, atlasSize);

    struct PendingChart {
        std::int32_t faceIndex = -1;
        int luxelW = 0;
        int luxelH = 0;
    };
    std::vector<PendingChart> pending;
    pending.reserve(faces.size());
    constexpr float kLargeFaceMeters = 4.0f;
    for (std::int32_t faceIndex = 0; faceIndex < static_cast<std::int32_t>(faces.size()); ++faceIndex) {
        const LightmapFace& face = faces[static_cast<std::size_t>(faceIndex)];
        Vector3 uAxis{};
        Vector3 vAxis{};
        faceUvAxes(face.uvLock, face.normal, face.uvUAxis, face.uvVAxis, uAxis, vAxis);
        const float widthMeters = faceExtent(face, uAxis);
        const float heightMeters = faceExtent(face, vAxis);
        float effectiveLpm = luxelsPerMeter;
        if (std::max(widthMeters, heightMeters) > kLargeFaceMeters) {
            effectiveLpm *= 0.5f;
        }
        PendingChart chart;
        chart.faceIndex = faceIndex;
        chart.luxelW = std::clamp(
            static_cast<int>(std::ceil(widthMeters * effectiveLpm)) + 2,
            2,
            atlasSize);
        chart.luxelH = std::clamp(
            static_cast<int>(std::ceil(heightMeters * effectiveLpm)) + 2,
            2,
            atlasSize);
        pending.push_back(chart);
    }
    std::sort(pending.begin(), pending.end(), [](const PendingChart& a, const PendingChart& b) {
        if (a.luxelH != b.luxelH) {
            return a.luxelH > b.luxelH;
        }
        if (a.luxelW != b.luxelW) {
            return a.luxelW > b.luxelW;
        }
        return a.faceIndex < b.faceIndex;
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

    for (const PendingChart& pendingChart : pending) {
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

        const LightmapFace& face = faces[static_cast<std::size_t>(pendingChart.faceIndex)];
        LightmapChart chart;
        chart.faceIndex = pendingChart.faceIndex;
        chart.faceId = face.id;
        chart.atlasIndex = atlasIndex;
        chart.luxelWidth = luxelW;
        chart.luxelHeight = luxelH;
        chart.atlasX = cursorX;
        chart.atlasY = cursorY;
        chart.u0 = (static_cast<float>(cursorX) + 0.5f) / static_cast<float>(atlasSize);
        chart.v0 = (static_cast<float>(cursorY) + 0.5f) / static_cast<float>(atlasSize);
        chart.u1 = (static_cast<float>(cursorX + luxelW) - 0.5f) / static_cast<float>(atlasSize);
        chart.v1 = (static_cast<float>(cursorY + luxelH) - 0.5f) / static_cast<float>(atlasSize);
        result.rad.charts.push_back(std::move(chart));

        cursorX += luxelW;
        rowHeight = std::max(rowHeight, luxelH);
    }

    if (result.rad.atlases.empty()) {
        ensureAtlas();
    }

    return result;
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
    if (magic != kRadMagic || version != kRadVersion) {
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
    return shader;
}

}
