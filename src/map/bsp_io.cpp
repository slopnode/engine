#include "map/bsp_io.hpp"

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

std::uint32_t internString(
    std::unordered_map<std::string, std::uint32_t>& table,
    std::vector<std::string>& strings,
    const std::string& value) {
    const auto existing = table.find(value);
    if (existing != table.end()) {
        return existing->second;
    }
    const std::uint32_t index = static_cast<std::uint32_t>(strings.size());
    table.emplace(value, index);
    strings.push_back(value);
    return index;
}

void writePolygon(BinaryWriter& writer, const std::vector<Vector3>& verts) {
    writer.writePod(static_cast<std::uint32_t>(verts.size()));
    for (const Vector3& v : verts) {
        writer.writePod(v);
    }
}

bool readPolygon(BinaryReader& reader, std::vector<Vector3>& verts) {
    std::uint32_t count = 0;
    if (!reader.readPod(count)) {
        return false;
    }
    verts.resize(count);
    for (Vector3& v : verts) {
        if (!reader.readPod(v)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool writeBspFile(const std::filesystem::path& path, const BspTree& tree) {
    std::unordered_map<std::string, std::uint32_t> stringTable;
    std::vector<std::string> strings;
    internString(stringTable, strings, "");

    BinaryWriter writer;
    writer.writePod(kBspMagic);
    writer.writePod(kBspVersion);
    writer.writePod(tree.root);
    writer.writePod(tree.boundsMins);
    writer.writePod(tree.boundsMaxs);

    writer.writePod(static_cast<std::uint32_t>(tree.nodes.size()));
    for (const BspNode& node : tree.nodes) {
        writer.writePod(node.plane.normal);
        writer.writePod(node.plane.distance);
        writer.writePod(node.front);
        writer.writePod(node.back);
    }

    writer.writePod(static_cast<std::uint32_t>(tree.leaves.size()));
    for (const BspLeaf& leaf : tree.leaves) {
        const std::uint8_t solid = leaf.solid ? 1 : 0;
        writer.writePod(solid);
        writer.writePod(leaf.mins);
        writer.writePod(leaf.maxs);
        writer.writePod(static_cast<std::uint32_t>(leaf.faces.size()));
        for (const auto& face : leaf.faces) {
            writePolygon(writer, face);
        }
        writer.writePod(static_cast<std::uint32_t>(leaf.neighbors.size()));
        for (std::int32_t neighbor : leaf.neighbors) {
            writer.writePod(neighbor);
        }
    }

    writer.writePod(static_cast<std::uint32_t>(tree.surfaceFaces.size()));
    std::vector<std::uint32_t> idIndices;
    std::vector<std::uint32_t> materialIndices;
    idIndices.reserve(tree.surfaceFaces.size());
    materialIndices.reserve(tree.surfaceFaces.size());
    for (const BspSurfaceFace& face : tree.surfaceFaces) {
        idIndices.push_back(internString(stringTable, strings, face.id));
        materialIndices.push_back(internString(stringTable, strings, face.material));
        writePolygon(writer, face.vertices);
        writer.writePod(face.normal);
        writer.writePod(face.emptyLeaf);
        writer.writePod(face.uvShiftPixels);
        writer.writePod(idIndices.back());
        writer.writePod(materialIndices.back());
    }

    writer.writePod(static_cast<std::uint32_t>(strings.size()));
    for (const std::string& value : strings) {
        writer.writeString(value);
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

std::optional<BspTree> readBspBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version)) {
        return std::nullopt;
    }
    if (magic != kBspMagic || version != kBspVersion) {
        return std::nullopt;
    }

    BspTree tree;
    if (!reader.readPod(tree.root) || !reader.readPod(tree.boundsMins) || !reader.readPod(tree.boundsMaxs)) {
        return std::nullopt;
    }

    std::uint32_t nodeCount = 0;
    if (!reader.readPod(nodeCount)) {
        return std::nullopt;
    }
    tree.nodes.resize(nodeCount);
    for (BspNode& node : tree.nodes) {
        if (!reader.readPod(node.plane.normal) || !reader.readPod(node.plane.distance)
            || !reader.readPod(node.front) || !reader.readPod(node.back)) {
            return std::nullopt;
        }
    }

    std::uint32_t leafCount = 0;
    if (!reader.readPod(leafCount)) {
        return std::nullopt;
    }
    tree.leaves.resize(leafCount);
    for (BspLeaf& leaf : tree.leaves) {
        std::uint8_t solid = 0;
        std::uint32_t faceCount = 0;
        std::uint32_t neighborCount = 0;
        if (!reader.readPod(solid) || !reader.readPod(leaf.mins) || !reader.readPod(leaf.maxs)
            || !reader.readPod(faceCount)) {
            return std::nullopt;
        }
        leaf.solid = solid != 0;
        leaf.faces.resize(faceCount);
        for (auto& face : leaf.faces) {
            if (!readPolygon(reader, face)) {
                return std::nullopt;
            }
        }
        if (!reader.readPod(neighborCount)) {
            return std::nullopt;
        }
        leaf.neighbors.resize(neighborCount);
        for (std::int32_t& neighbor : leaf.neighbors) {
            if (!reader.readPod(neighbor)) {
                return std::nullopt;
            }
        }
    }

    std::uint32_t faceCount = 0;
    if (!reader.readPod(faceCount)) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> idIndices(faceCount);
    std::vector<std::uint32_t> materialIndices(faceCount);
    tree.surfaceFaces.resize(faceCount);
    for (std::uint32_t i = 0; i < faceCount; ++i) {
        BspSurfaceFace& face = tree.surfaceFaces[i];
        if (!readPolygon(reader, face.vertices)
            || !reader.readPod(face.normal) || !reader.readPod(face.emptyLeaf)
            || !reader.readPod(face.uvShiftPixels) || !reader.readPod(idIndices[i])
            || !reader.readPod(materialIndices[i])) {
            return std::nullopt;
        }
    }

    std::uint32_t stringCount = 0;
    if (!reader.readPod(stringCount)) {
        return std::nullopt;
    }
    std::vector<std::string> strings(stringCount);
    for (std::string& value : strings) {
        if (!reader.readString(value)) {
            return std::nullopt;
        }
    }

    for (std::uint32_t i = 0; i < faceCount; ++i) {
        if (idIndices[i] >= stringCount || materialIndices[i] >= stringCount) {
            return std::nullopt;
        }
        tree.surfaceFaces[i].id = strings[idIndices[i]];
        tree.surfaceFaces[i].material = strings[materialIndices[i]];
    }

    return tree;
}

std::optional<BspTree> readBspFile(const std::filesystem::path& path) {
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
    return readBspBytes(buffer);
}

}
