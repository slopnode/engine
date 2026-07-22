#include "map/vis_io.hpp"

#include <raylib.h>

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

bool writeVisFile(const std::filesystem::path& path, const VisFile& vis) {
    std::unordered_map<std::string, std::uint32_t> stringTable;
    std::vector<std::string> strings;
    internString(stringTable, strings, "");

    BinaryWriter writer;
    writer.writePod(kVisMagic);
    writer.writePod(kVisVersion);
    writer.writePod(static_cast<std::uint32_t>(vis.faces.size()));

    for (const VisibleFace& face : vis.faces) {
        writePolygon(writer, face.vertices);
        writer.writePod(face.normal);
        writer.writePod(face.uvShiftPixels);
        writer.writePod(face.uvScale);
        writer.writePod(face.uvUAxis);
        writer.writePod(face.uvVAxis);
        writer.writePod(static_cast<std::uint8_t>(face.uvLock ? 1 : 0));
        writer.writePod(face.interiorLeaf);
        writer.writePod(internString(stringTable, strings, face.id));
        writer.writePod(internString(stringTable, strings, face.sourceFaceId));
        writer.writePod(internString(stringTable, strings, face.material));
    }

    writer.writePod(static_cast<std::uint32_t>(strings.size()));
    for (const std::string& value : strings) {
        writer.writeString(value);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(writer.buffer().data()),
        static_cast<std::streamsize>(writer.buffer().size()));
    return static_cast<bool>(out);
}

std::optional<VisFile> readVisBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version)) {
        return std::nullopt;
    }
    if (magic != kVisMagic || version != kVisVersion) {
        return std::nullopt;
    }

    std::uint32_t faceCount = 0;
    if (!reader.readPod(faceCount)) {
        return std::nullopt;
    }

    struct FaceRecord {
        std::vector<Vector3> vertices;
        Vector3 normal{};
        Vector2 uvShiftPixels{};
        Vector2 uvScale{1.0f, 1.0f};
        Vector3 uvUAxis{};
        Vector3 uvVAxis{};
        std::uint8_t uvLock = 0;
        std::int32_t interiorLeaf = -1;
        std::uint32_t idIndex = 0;
        std::uint32_t sourceIndex = 0;
        std::uint32_t materialIndex = 0;
    };

    std::vector<FaceRecord> records(faceCount);
    for (FaceRecord& record : records) {
        if (!readPolygon(reader, record.vertices)
            || !reader.readPod(record.normal)
            || !reader.readPod(record.uvShiftPixels)
            || !reader.readPod(record.uvScale)
            || !reader.readPod(record.uvUAxis)
            || !reader.readPod(record.uvVAxis)
            || !reader.readPod(record.uvLock)
            || !reader.readPod(record.interiorLeaf)
            || !reader.readPod(record.idIndex)
            || !reader.readPod(record.sourceIndex)
            || !reader.readPod(record.materialIndex)) {
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

    auto resolve = [&](std::uint32_t index) -> std::string {
        if (index >= strings.size()) {
            return {};
        }
        return strings[index];
    };

    VisFile vis;
    vis.faces.reserve(records.size());
    for (const FaceRecord& record : records) {
        VisibleFace face;
        face.vertices = record.vertices;
        face.normal = record.normal;
        face.uvShiftPixels = record.uvShiftPixels;
        face.uvScale = record.uvScale;
        face.uvUAxis = record.uvUAxis;
        face.uvVAxis = record.uvVAxis;
        face.uvLock = record.uvLock != 0;
        face.interiorLeaf = record.interiorLeaf;
        face.id = resolve(record.idIndex);
        face.sourceFaceId = resolve(record.sourceIndex);
        face.material = resolve(record.materialIndex);
        vis.faces.push_back(std::move(face));
    }
    return vis;
}

std::optional<VisFile> readVisFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    const auto size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in) {
            return std::nullopt;
        }
    }
    return readVisBytes(bytes);
}

}
