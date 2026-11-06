#include "map/pvs_io.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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

    const std::vector<std::byte>& buffer() const {
        return buffer_;
    }

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

private:
    std::span<const std::byte> data_;
    std::size_t cursor_ = 0;
};

template <typename T>
bool readPodArray(BinaryReader& reader, std::vector<T>& out, std::size_t count) {
    out.resize(count);
    return count == 0 || reader.readBytes(out.data(), count * sizeof(T));
}

std::optional<PvsFile> readDensePvs(BinaryReader& reader) {
    std::uint32_t leafCount = 0;
    std::uint32_t wordsPerRow = 0;
    if (!reader.readPod(leafCount) || !reader.readPod(wordsPerRow)) {
        return std::nullopt;
    }
    if (leafCount > 1'000'000u || wordsPerRow > 100'000u) {
        return std::nullopt;
    }
    DensePvs dense;
    dense.leafCount = static_cast<int>(leafCount);
    dense.wordsPerRow = static_cast<int>(wordsPerRow);
    if (!readPodArray(
            reader,
            dense.bits,
            static_cast<std::size_t>(leafCount) * static_cast<std::size_t>(wordsPerRow))) {
        return std::nullopt;
    }
    return compactPvs(dense);
}

std::optional<PvsFile> readCompactPvs(BinaryReader& reader) {
    std::uint32_t leafCount = 0;
    std::uint32_t columnCount = 0;
    std::uint32_t wordsPerRow = 0;
    std::uint32_t rowCount = 0;
    if (!reader.readPod(leafCount) || !reader.readPod(columnCount) ||
        !reader.readPod(wordsPerRow) || !reader.readPod(rowCount)) {
        return std::nullopt;
    }
    if (leafCount > 1'000'000u || columnCount > leafCount || rowCount > leafCount ||
        wordsPerRow != (columnCount + 31u) / 32u) {
        return std::nullopt;
    }
    PvsFile pvs;
    pvs.leafCount = static_cast<int>(leafCount);
    pvs.columnCount = static_cast<int>(columnCount);
    pvs.wordsPerRow = static_cast<int>(wordsPerRow);
    if (leafCount == 0) {
        return pvs;
    }
    if (!readPodArray(reader, pvs.leafRow, leafCount) ||
        !readPodArray(reader, pvs.leafColumn, leafCount) ||
        !readPodArray(
            reader,
            pvs.rows,
            static_cast<std::size_t>(rowCount) * static_cast<std::size_t>(wordsPerRow))) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < leafCount; ++i) {
        const std::int32_t row = pvs.leafRow[i];
        const std::int32_t column = pvs.leafColumn[i];
        if (row < -1 || row >= static_cast<std::int32_t>(rowCount) || column < -1 ||
            column >= static_cast<std::int32_t>(columnCount)) {
            return std::nullopt;
        }
    }
    return pvs;
}

} // namespace

bool writePvsFile(const std::filesystem::path& path, const PvsFile& pvs) {
    BinaryWriter writer;
    writer.writePod(kPvsMagic);
    writer.writePod(kPvsVersion);
    writer.writePod(static_cast<std::uint32_t>(pvs.leafCount));
    writer.writePod(static_cast<std::uint32_t>(pvs.columnCount));
    writer.writePod(static_cast<std::uint32_t>(pvs.wordsPerRow));
    writer.writePod(static_cast<std::uint32_t>(pvs.rowCount()));
    if (pvs.leafCount > 0) {
        writer.writeBytes(pvs.leafRow.data(), pvs.leafRow.size() * sizeof(std::int32_t));
        writer.writeBytes(pvs.leafColumn.data(), pvs.leafColumn.size() * sizeof(std::int32_t));
        writer.writeBytes(pvs.rows.data(), pvs.rows.size() * sizeof(std::uint32_t));
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(writer.buffer().data()),
        static_cast<std::streamsize>(writer.buffer().size()));
    return static_cast<bool>(out);
}

std::optional<PvsFile> readPvsBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version) || magic != kPvsMagic) {
        return std::nullopt;
    }
    if (version == kPvsDenseVersion) {
        return readDensePvs(reader);
    }
    if (version == kPvsVersion) {
        return readCompactPvs(reader);
    }
    return std::nullopt;
}

std::optional<PvsFile> readPvsFile(const std::filesystem::path& path) {
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
    return readPvsBytes(bytes);
}

}
