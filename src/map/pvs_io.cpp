#include "map/pvs_io.hpp"

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

} // namespace

bool writePvsFile(const std::filesystem::path& path, const PvsFile& pvs) {
    BinaryWriter writer;
    writer.writePod(kPvsMagic);
    writer.writePod(kPvsVersion);
    writer.writePod(static_cast<std::uint32_t>(pvs.leafCount));
    writer.writePod(static_cast<std::uint32_t>(pvs.wordsPerRow));
    for (std::uint32_t word : pvs.bits) {
        writer.writePod(word);
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

std::optional<PvsFile> readPvsBytes(std::span<const std::byte> data) {
    BinaryReader reader(data);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!reader.readPod(magic) || !reader.readPod(version)) {
        return std::nullopt;
    }
    if (magic != kPvsMagic || version != kPvsVersion) {
        return std::nullopt;
    }

    std::uint32_t leafCount = 0;
    std::uint32_t wordsPerRow = 0;
    if (!reader.readPod(leafCount) || !reader.readPod(wordsPerRow)) {
        return std::nullopt;
    }
    if (leafCount > 1'000'000u || wordsPerRow > 100'000u) {
        return std::nullopt;
    }
    const std::size_t expectedWords =
        static_cast<std::size_t>(leafCount) * static_cast<std::size_t>(wordsPerRow);
    PvsFile pvs;
    pvs.leafCount = static_cast<int>(leafCount);
    pvs.wordsPerRow = static_cast<int>(wordsPerRow);
    pvs.bits.resize(expectedWords);
    for (std::uint32_t& word : pvs.bits) {
        if (!reader.readPod(word)) {
            return std::nullopt;
        }
    }
    return pvs;
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
