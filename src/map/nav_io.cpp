#include "map/nav_io.hpp"

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

void rebuildReverseAdjacency(MapNavigation& nav) {
    nav.reverseAdjacency.assign(static_cast<std::size_t>(nav.leafCount), {});
    for (int i = 0; i < nav.leafCount; ++i) {
        for (const NavPortalLink& link : nav.adjacency[static_cast<std::size_t>(i)]) {
            if (link.neighborLeaf < 0 || link.neighborLeaf >= nav.leafCount) {
                continue;
            }
            nav.reverseAdjacency[static_cast<std::size_t>(link.neighborLeaf)].push_back(
                NavPortalLink{
                    i, link.portalCenter, link.portalTangent, link.portalHalfWidth, link.cost,
                    link.doorBrushId, link.climbHeight});
        }
    }
}

} // namespace

bool writeNavFile(const std::filesystem::path& path, const MapNavigation& nav) {
    std::unordered_map<std::string, std::uint32_t> stringTable;
    std::vector<std::string> strings;
    internString(stringTable, strings, "");

    BinaryWriter writer;
    writer.writePod(kNavMagic);
    writer.writePod(kNavVersion);
    writer.writePod(static_cast<std::uint32_t>(nav.leafCount));

    for (int i = 0; i < nav.leafCount; ++i) {
        writer.writePod(static_cast<std::uint8_t>(nav.walkable[static_cast<std::size_t>(i)] ? 1 : 0));
        writer.writePod(static_cast<std::uint8_t>(nav.leafIsWater[static_cast<std::size_t>(i)] ? 1 : 0));
        writer.writePod(nav.leafCentroids[static_cast<std::size_t>(i)]);
        writer.writePod(nav.leafFloorY[static_cast<std::size_t>(i)]);
        writer.writePod(nav.leafCeilingY[static_cast<std::size_t>(i)]);

        const bool hasBoundary = i < static_cast<int>(nav.leafBoundary.size());
        const auto& boundary = hasBoundary ? nav.leafBoundary[static_cast<std::size_t>(i)]
                                            : std::vector<Vector3>{};
        writer.writePod(static_cast<std::uint32_t>(boundary.size()));
        for (const Vector3& v : boundary) {
            writer.writePod(v);
        }
    }

    for (int i = 0; i < nav.leafCount; ++i) {
        const auto& links = nav.adjacency[static_cast<std::size_t>(i)];
        writer.writePod(static_cast<std::uint32_t>(links.size()));
        for (const NavPortalLink& link : links) {
            writer.writePod(link.neighborLeaf);
            writer.writePod(link.portalCenter);
            writer.writePod(link.portalTangent);
            writer.writePod(link.portalHalfWidth);
            writer.writePod(link.cost);
            writer.writePod(internString(stringTable, strings, link.doorBrushId));
            writer.writePod(link.climbHeight);
        }
    }

    writer.writePod(static_cast<std::uint32_t>(strings.size()));
    for (const std::string& value : strings) {
        writer.writeString(value);
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        TraceLog(LOG_WARNING, "NAV: failed to open for write '%s'", path.string().c_str());
        return false;
    }
    file.write(
        reinterpret_cast<const char*>(writer.buffer().data()),
        static_cast<std::streamsize>(writer.buffer().size()));
    if (!file) {
        TraceLog(LOG_WARNING, "NAV: failed to write '%s'", path.string().c_str());
        return false;
    }

    TraceLog(
        LOG_INFO,
        "NAV: wrote '%s' bytes=%d leaves=%d",
        path.string().c_str(),
        static_cast<int>(writer.buffer().size()),
        nav.leafCount);
    return true;
}

std::optional<MapNavigation> readNavBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version)) {
        TraceLog(LOG_WARNING, "NAV: truncated header");
        return std::nullopt;
    }
    if (magic != kNavMagic) {
        TraceLog(LOG_WARNING, "NAV: bad magic 0x%08x (need 0x%08x)", magic, kNavMagic);
        return std::nullopt;
    }
    if (version != kNavVersion) {
        TraceLog(
            LOG_WARNING,
            "NAV: unsupported version %u (need %u); rebuild map tools / recompile nav",
            version,
            kNavVersion);
        return std::nullopt;
    }

    std::uint32_t leafCount = 0;
    if (!reader.readPod(leafCount)) {
        return std::nullopt;
    }

    MapNavigation nav;
    nav.leafCount = static_cast<int>(leafCount);
    nav.walkable.resize(leafCount);
    nav.leafIsWater.resize(leafCount);
    nav.leafCentroids.resize(leafCount);
    nav.leafFloorY.resize(leafCount);
    nav.leafCeilingY.resize(leafCount);
    nav.leafBoundary.resize(leafCount);

    for (std::uint32_t i = 0; i < leafCount; ++i) {
        std::uint8_t walkable = 0;
        std::uint8_t isWater = 0;
        if (!reader.readPod(walkable) || !reader.readPod(isWater)
            || !reader.readPod(nav.leafCentroids[i]) || !reader.readPod(nav.leafFloorY[i])
            || !reader.readPod(nav.leafCeilingY[i])) {
            return std::nullopt;
        }
        nav.walkable[i] = walkable != 0;
        nav.leafIsWater[i] = isWater != 0;

        std::uint32_t boundaryCount = 0;
        if (!reader.readPod(boundaryCount)) {
            return std::nullopt;
        }
        nav.leafBoundary[i].resize(boundaryCount);
        for (Vector3& v : nav.leafBoundary[i]) {
            if (!reader.readPod(v)) {
                return std::nullopt;
            }
        }
    }

    nav.adjacency.resize(leafCount);
    std::vector<std::vector<std::uint32_t>> doorBrushIndices(leafCount);
    for (std::uint32_t i = 0; i < leafCount; ++i) {
        std::uint32_t linkCount = 0;
        if (!reader.readPod(linkCount)) {
            return std::nullopt;
        }
        nav.adjacency[i].resize(linkCount);
        doorBrushIndices[i].resize(linkCount);
        for (std::uint32_t j = 0; j < linkCount; ++j) {
            NavPortalLink& link = nav.adjacency[i][j];
            if (!reader.readPod(link.neighborLeaf) || !reader.readPod(link.portalCenter)
                || !reader.readPod(link.portalTangent) || !reader.readPod(link.portalHalfWidth)
                || !reader.readPod(link.cost) || !reader.readPod(doorBrushIndices[i][j])
                || !reader.readPod(link.climbHeight)) {
                return std::nullopt;
            }
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

    for (std::uint32_t i = 0; i < leafCount; ++i) {
        for (std::uint32_t j = 0; j < doorBrushIndices[i].size(); ++j) {
            const std::uint32_t idx = doorBrushIndices[i][j];
            if (idx >= stringCount) {
                return std::nullopt;
            }
            nav.adjacency[i][j].doorBrushId = strings[idx];
        }
    }

    rebuildReverseAdjacency(nav);
    return nav;
}

std::optional<MapNavigation> readNavFile(const std::filesystem::path& path) {
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
    return readNavBytes(bytes);
}

}
