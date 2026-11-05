#include "assets/geo_loader.hpp"

#include <charconv>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

#include <raymath.h>

namespace slopengine {

namespace {

constexpr char kVertMagic[] = {'D', 'L', 'K', 'V'};
constexpr char kWeightsMagic[] = {'D', 'L', 'K', 'W'};
constexpr std::uint16_t kVertVersion = 1;
constexpr std::uint16_t kWeightsVersion = 1;
constexpr std::uint16_t kFlagNormals = 1;
constexpr std::uint16_t kFlagUvs = 2;

template<typename T>
bool readScalar(std::span<const std::byte> data, std::size_t& offset, T& value) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool readFloats(std::span<const std::byte> data, std::size_t& offset, std::size_t count, float* out) {
    const std::size_t byteCount = count * sizeof(float);
    if (offset + byteCount > data.size()) {
        return false;
    }
    std::memcpy(out, data.data() + offset, byteCount);
    offset += byteCount;
    return true;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::string> readQuotedField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteStart = line.find('"', prefixPos + prefix.size());
    if (quoteStart == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t quoteEnd = line.find('"', quoteStart + 1);
    if (quoteEnd == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string{line.substr(quoteStart + 1, quoteEnd - quoteStart - 1)};
}

std::optional<std::size_t> readUintField(std::string_view line, std::string_view prefix) {
    const std::size_t prefixPos = line.find(prefix);
    if (prefixPos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view value = trim(line.substr(prefixPos + prefix.size()));
    std::size_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr == begin) {
        return std::nullopt;
    }

    return parsed;
}

bool startsWith(std::string_view line, std::string_view prefix) {
    return line.rfind(prefix, 0) == 0;
}

bool readFloatsField(std::string_view line, std::string_view prefix, float* out, int count) {
    if (!startsWith(line, prefix)) {
        return false;
    }
    std::string_view rest = trim(line.substr(prefix.size()));
    for (int i = 0; i < count; ++i) {
        rest = trim(rest);
        const auto* begin = rest.data();
        const auto* end = rest.data() + rest.size();
        const auto result = std::from_chars(begin, end, out[i]);
        if (result.ec != std::errc{} || result.ptr == begin) {
            return false;
        }
        rest.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    }
    return true;
}

enum class GeoSection {
    Header,
    View,
    Attachments,
    Primitives,
};

} // namespace

bool parseGeoAsset(std::string_view source, GeoAsset& asset) {
    asset = {};
    asset.verticesImplicit = true;

    GeoPrimitive primitive{};
    GeoAttachment attachment{};
    bool inEntry = false;
    GeoSection section = GeoSection::Header;

    auto flushEntry = [&]() {
        if (!inEntry) {
            return;
        }
        if (section == GeoSection::Attachments) {
            asset.attachments.push_back(attachment);
            attachment = {};
        } else {
            asset.primitives.push_back(primitive);
            primitive = {};
        }
        inEntry = false;
    };

    std::size_t lineStart = 0;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        const std::string_view line = trim(source.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos : lineEnd - lineStart));

        if (lineEnd == std::string_view::npos && line.empty()) {
            break;
        }

        if (startsWith(line, "(vertices implicit)")) {
            asset.verticesImplicit = true;
        } else if (startsWith(line, "(weights implicit)")) {
            asset.weightsImplicit = true;
        } else if (auto skeletonId = readQuotedField(line, "(skeleton ")) {
            asset.skeletonId = *skeletonId;
        } else if (startsWith(line, "(view")) {
            flushEntry();
            section = GeoSection::View;
            asset.view.present = true;
        } else if (startsWith(line, "(attachments")) {
            flushEntry();
            section = GeoSection::Attachments;
        } else if (startsWith(line, "(primitives")) {
            flushEntry();
            section = GeoSection::Primitives;
        } else if (section == GeoSection::View) {
            if (startsWith(line, ")")) {
                section = GeoSection::Header;
            } else if (
                !readFloatsField(line, "position ", &asset.view.position.x, 3) &&
                !readFloatsField(line, "rotation ", &asset.view.rotationDeg.x, 3) &&
                !readFloatsField(line, "scale ", &asset.view.scale.x, 3) &&
                !line.empty()) {
                return false;
            }
        } else if (startsWith(line, "(name ")) {
            flushEntry();
            if (section == GeoSection::Header) {
                section = GeoSection::Primitives;
            }
            if (section == GeoSection::Attachments) {
                if (auto name = readQuotedField(line, "(name ")) {
                    attachment.name = *name;
                }
            } else if (auto name = readQuotedField(line, "(name ")) {
                primitive.name = *name;
            }
            inEntry = true;
        } else if (inEntry && section == GeoSection::Attachments) {
            if (auto bone = readQuotedField(line, "bone ")) {
                attachment.bone = *bone;
            } else if (readFloatsField(line, "position ", &attachment.position.x, 3)) {
            } else if (readFloatsField(line, "rotation ", &attachment.rotationDeg.x, 3)) {
            } else if (readFloatsField(line, "scale ", &attachment.scale, 1)) {
            } else if (startsWith(line, ")")) {
                flushEntry();
            }
        } else if (inEntry) {
            if (auto material = readQuotedField(line, "material ")) {
                primitive.material = *material;
            } else if (auto vertexOffset = readUintField(line, "vertex-offset ")) {
                primitive.vertexOffset = *vertexOffset;
            } else if (auto vertexCount = readUintField(line, "vertex-count ")) {
                primitive.vertexCount = *vertexCount;
            } else if (auto indexOffset = readUintField(line, "index-offset ")) {
                primitive.indexOffset = *indexOffset;
            } else if (auto indexCount = readUintField(line, "index-count ")) {
                primitive.indexCount = *indexCount;
            } else if (startsWith(line, ")")) {
                flushEntry();
            }
        } else if (startsWith(line, ")") && section != GeoSection::Header) {
            section = GeoSection::Header;
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    flushEntry();

    return !asset.primitives.empty();
}

std::string serializeGeoAsset(const GeoAsset& asset) {
    std::ostringstream out;
    out << "(geo\n";
    out << "  (vertices implicit)\n";
    if (asset.weightsImplicit) {
        out << "  (weights implicit)\n";
    }
    if (!asset.skeletonId.empty()) {
        out << "  (skeleton \"" << asset.skeletonId << "\")\n";
    }
    if (asset.view.present) {
        out << "  (view\n";
        out << "   position " << asset.view.position.x << ' ' << asset.view.position.y << ' '
            << asset.view.position.z << "\n";
        out << "   rotation " << asset.view.rotationDeg.x << ' ' << asset.view.rotationDeg.y << ' '
            << asset.view.rotationDeg.z << "\n";
        out << "   scale " << asset.view.scale.x << ' ' << asset.view.scale.y << ' '
            << asset.view.scale.z << "\n";
        out << "  )\n";
    }
    if (!asset.attachments.empty()) {
        out << "  (attachments\n";
        out << "   (\n";
        for (const GeoAttachment& attachment : asset.attachments) {
            out << "    (name \"" << attachment.name << "\"\n";
            out << "     bone \"" << attachment.bone << "\"\n";
            out << "     position " << attachment.position.x << ' ' << attachment.position.y << ' '
                << attachment.position.z << "\n";
            out << "     rotation " << attachment.rotationDeg.x << ' ' << attachment.rotationDeg.y
                << ' ' << attachment.rotationDeg.z << "\n";
            out << "     scale " << attachment.scale << "\n";
            out << "    )\n";
        }
        out << "   ))\n";
    }
    out << "  (primitives\n";
    out << "   (\n";
    for (const GeoPrimitive& primitive : asset.primitives) {
        out << "    (name \"" << primitive.name << "\"\n";
        out << "     material \"" << primitive.material << "\"\n";
        out << "     vertex-offset " << primitive.vertexOffset << "\n";
        out << "     vertex-count " << primitive.vertexCount << "\n";
        out << "     index-offset " << primitive.indexOffset << "\n";
        out << "     index-count " << primitive.indexCount << "\n";
        out << "    )\n";
    }
    out << "   ))\n";
    out << ")\n";
    return out.str();
}

const GeoAttachment* findGeoAttachment(const GeoAsset& asset, std::string_view name) {
    for (const GeoAttachment& attachment : asset.attachments) {
        if (attachment.name == name) {
            return &attachment;
        }
    }
    return nullptr;
}

Transform geoAttachmentTransform(const GeoAttachment& attachment) {
    Transform transform{};
    transform.translation = attachment.position;
    transform.rotation = QuaternionFromEuler(
        attachment.rotationDeg.x * DEG2RAD,
        attachment.rotationDeg.y * DEG2RAD,
        attachment.rotationDeg.z * DEG2RAD);
    transform.scale = {attachment.scale, attachment.scale, attachment.scale};
    return transform;
}

Transform geoViewTransform(const GeoView& view) {
    Transform transform{};
    transform.translation = view.position;
    transform.rotation = QuaternionFromEuler(
        view.rotationDeg.x * DEG2RAD, view.rotationDeg.y * DEG2RAD, view.rotationDeg.z * DEG2RAD);
    transform.scale = view.scale;
    return transform;
}

bool loadVertBuffer(std::span<const std::byte> data, VertBuffer& buffer) {
    buffer = {};

    if (data.size() < 4 + sizeof(std::uint16_t) + sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t)) {
        return false;
    }

    if (std::memcmp(data.data(), kVertMagic, 4) != 0) {
        return false;
    }

    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint16_t flags = 0;

    if (!readScalar(data, offset, version) || version != kVertVersion) {
        return false;
    }
    if (!readScalar(data, offset, vertexCount) || !readScalar(data, offset, indexCount) || !readScalar(data, offset, flags)) {
        return false;
    }

    buffer.positions.resize(vertexCount);
    if (!readFloats(
            data,
            offset,
            vertexCount * 3,
            reinterpret_cast<float*>(buffer.positions.data()))) {
        return false;
    }

    if ((flags & kFlagNormals) != 0) {
        buffer.normals.resize(vertexCount);
        if (!readFloats(
                data,
                offset,
                vertexCount * 3,
                reinterpret_cast<float*>(buffer.normals.data()))) {
            return false;
        }
    }

    if ((flags & kFlagUvs) != 0) {
        buffer.texcoords.resize(vertexCount);
        if (!readFloats(
                data,
                offset,
                vertexCount * 2,
                reinterpret_cast<float*>(buffer.texcoords.data()))) {
            return false;
        }
    }

    buffer.indices.resize(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        std::uint32_t value = 0;
        if (!readScalar(data, offset, value)) {
            return false;
        }
        buffer.indices[index] = value;
    }

    return true;
}

bool loadWeightsBuffer(std::span<const std::byte> data, std::vector<VertexWeights>& buffer) {
    buffer.clear();

    if (data.size() < 4 + sizeof(std::uint16_t) + sizeof(std::uint32_t)) {
        return false;
    }
    if (std::memcmp(data.data(), kWeightsMagic, 4) != 0) {
        return false;
    }

    std::size_t offset = 4;
    std::uint16_t version = 0;
    std::uint32_t vertexCount = 0;
    if (!readScalar(data, offset, version) || version != kWeightsVersion) {
        return false;
    }
    if (!readScalar(data, offset, vertexCount)) {
        return false;
    }

    buffer.resize(vertexCount);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        VertexWeights& weights = buffer[vertex];
        for (int index = 0; index < 4; ++index) {
            std::byte value = std::byte{0};
            if (!readScalar(data, offset, value)) {
                return false;
            }
            weights.jointIndices[index] = static_cast<unsigned char>(value);
        }
        for (int index = 0; index < 4; ++index) {
            if (!readScalar(data, offset, weights.jointWeights[index])) {
                return false;
            }
        }
    }

    return true;
}

namespace {

Mesh buildPrimitiveMesh(
    const VertBuffer& buffer,
    const GeoPrimitive& primitive,
    const std::vector<VertexWeights>* weights,
    int boneCount) {
    Mesh mesh = {};

    if (primitive.vertexCount == 0 || primitive.indexCount < 3) {
        return mesh;
    }

    if (primitive.vertexOffset + primitive.vertexCount > buffer.positions.size()) {
        return mesh;
    }
    if (primitive.indexOffset + primitive.indexCount > buffer.indices.size()) {
        return mesh;
    }

    mesh.vertexCount = static_cast<int>(primitive.vertexCount);
    mesh.triangleCount = static_cast<int>(primitive.indexCount / 3);

    mesh.vertices = static_cast<float*>(RL_MALLOC(mesh.vertexCount * 3 * sizeof(float)));
    std::memcpy(
        mesh.vertices,
        buffer.positions.data() + primitive.vertexOffset,
        mesh.vertexCount * 3 * sizeof(float));

    if (buffer.normals.size() >= primitive.vertexOffset + primitive.vertexCount) {
        mesh.normals = static_cast<float*>(RL_MALLOC(mesh.vertexCount * 3 * sizeof(float)));
        std::memcpy(
            mesh.normals,
            buffer.normals.data() + primitive.vertexOffset,
            mesh.vertexCount * 3 * sizeof(float));
    }

    if (buffer.texcoords.size() >= primitive.vertexOffset + primitive.vertexCount) {
        mesh.texcoords = static_cast<float*>(RL_MALLOC(mesh.vertexCount * 2 * sizeof(float)));
        std::memcpy(
            mesh.texcoords,
            buffer.texcoords.data() + primitive.vertexOffset,
            mesh.vertexCount * 2 * sizeof(float));
    }

    if (buffer.texcoords2.size() >= primitive.vertexOffset + primitive.vertexCount) {
        mesh.texcoords2 = static_cast<float*>(RL_MALLOC(mesh.vertexCount * 2 * sizeof(float)));
        std::memcpy(
            mesh.texcoords2,
            buffer.texcoords2.data() + primitive.vertexOffset,
            mesh.vertexCount * 2 * sizeof(float));
    }

    if (buffer.colors.size() >= primitive.vertexOffset + primitive.vertexCount) {
        mesh.colors = static_cast<unsigned char*>(RL_MALLOC(mesh.vertexCount * 4 * sizeof(unsigned char)));
        std::memcpy(
            mesh.colors,
            buffer.colors.data() + primitive.vertexOffset,
            mesh.vertexCount * 4 * sizeof(unsigned char));
    }

    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(primitive.indexCount * sizeof(unsigned short)));
    for (std::size_t index = 0; index < primitive.indexCount; ++index) {
        const std::uint32_t sourceIndex = buffer.indices[primitive.indexOffset + index];
        const std::uint32_t localIndex = sourceIndex - static_cast<std::uint32_t>(primitive.vertexOffset);
        mesh.indices[index] = static_cast<unsigned short>(localIndex);
    }

    if (weights != nullptr &&
        weights->size() >= primitive.vertexOffset + primitive.vertexCount &&
        boneCount > 0) {
        mesh.boneCount = boneCount;
        mesh.boneIndices =
            static_cast<unsigned char*>(RL_MALLOC(mesh.vertexCount * 4 * sizeof(unsigned char)));
        mesh.boneWeights = static_cast<float*>(RL_MALLOC(mesh.vertexCount * 4 * sizeof(float)));
        for (int vertex = 0; vertex < mesh.vertexCount; ++vertex) {
            const VertexWeights& source =
                (*weights)[static_cast<std::size_t>(primitive.vertexOffset) + static_cast<std::size_t>(vertex)];
            for (int influence = 0; influence < 4; ++influence) {
                const int offset = vertex * 4 + influence;
                mesh.boneIndices[offset] = source.jointIndices[influence];
                mesh.boneWeights[offset] = source.jointWeights[influence];
            }
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

} // namespace

Model buildModelFromGeo(
    const GeoAsset& asset,
    const VertBuffer& buffer,
    const MaterialResolver& resolveMaterial,
    const std::vector<VertexWeights>* weights,
    int skeletonBoneCount) {
    Model model = {};

    if (asset.primitives.empty()) {
        return model;
    }

    const int boneCount = skeletonBoneCount;

    model.meshCount = static_cast<int>(asset.primitives.size());
    model.meshes = static_cast<Mesh*>(RL_CALLOC(model.meshCount, sizeof(Mesh)));
    model.materialCount = model.meshCount;
    model.materials = static_cast<Material*>(RL_CALLOC(model.materialCount, sizeof(Material)));
    model.meshMaterial = static_cast<int*>(RL_CALLOC(model.meshCount, sizeof(int)));

    int loadedMeshes = 0;
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        const GeoPrimitive& primitive = asset.primitives[static_cast<std::size_t>(meshIndex)];
        Mesh mesh = buildPrimitiveMesh(buffer, primitive, weights, boneCount);
        if (mesh.vertexCount <= 0 || mesh.triangleCount <= 0) {
            continue;
        }

        model.meshes[loadedMeshes] = mesh;
        if (resolveMaterial) {
            model.materials[loadedMeshes] = resolveMaterial(primitive.material);
        } else {
            model.materials[loadedMeshes] = LoadMaterialDefault();
        }
        model.meshMaterial[loadedMeshes] = loadedMeshes;
        ++loadedMeshes;
    }

    model.meshCount = loadedMeshes;
    model.materialCount = loadedMeshes;

    if (loadedMeshes == 0) {
        UnloadModel(model);
        return Model{};
    }

    model.transform = MatrixIdentity();
    return model;
}

}
