#include "net/net_message.hpp"

#include <cmath>
#include <cstring>

namespace slopengine {

namespace {


std::uint64_t realSteps(const NetType& type, double value) {
    return static_cast<std::uint64_t>(std::llround((value - type.realLo) / type.step));
}

std::uint64_t realStepCount(const NetType& type) {
    return static_cast<std::uint64_t>(std::llround((type.realHi - type.realLo) / type.step));
}

} // namespace

void NetWriter::u8(std::uint8_t value) {
    data_.push_back(static_cast<std::byte>(value));
}

void NetWriter::u64(std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        u8(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void NetWriter::varint(std::uint64_t value) {
    while (value >= 0x80) {
        u8(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    u8(static_cast<std::uint8_t>(value));
}

void NetWriter::f32(float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    for (int i = 0; i < 4; ++i) {
        u8(static_cast<std::uint8_t>(raw >> (8 * i)));
    }
}

void NetWriter::string(std::string_view value) {
    varint(value.size());
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    data_.insert(data_.end(), begin, begin + value.size());
}

void NetWriter::bits(const std::vector<char>& flags) {
    varint(flags.size());
    std::uint8_t pending = 0;
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (flags[i] != 0) {
            pending |= static_cast<std::uint8_t>(1u << (i % 8));
        }
        if (i % 8 == 7) {
            u8(pending);
            pending = 0;
        }
    }
    if (flags.size() % 8 != 0) {
        u8(pending);
    }
}

void NetWriter::value(const NetValue& value, const NetType& type) {
    switch (type.kind) {
    case NetTypeKind::Bool:
        u8(value.flag ? 1 : 0);
        break;
    case NetTypeKind::Int:
        varint(static_cast<std::uint64_t>(value.integer - type.lo));
        break;
    case NetTypeKind::Real:
        varint(realSteps(type, value.real));
        break;
    case NetTypeKind::Symbol: {
        std::size_t index = 0;
        while (index < type.symbols.size() && type.symbols[index] != value.text) {
            ++index;
        }
        varint(index);
        break;
    }
    case NetTypeKind::Float:
        f32(static_cast<float>(value.real));
        break;
    case NetTypeKind::String:
    case NetTypeKind::Entity:
        string(value.text);
        break;
    case NetTypeKind::List:
        varint(value.items.size());
        for (const NetValue& item : value.items) {
            this->value(item, type.items[0]);
        }
        break;
    case NetTypeKind::Table:
        varint(value.items.size() / 2);
        for (std::size_t i = 0; i + 1 < value.items.size(); i += 2) {
            this->value(value.items[i], type.items[0]);
            this->value(value.items[i + 1], type.items[1]);
        }
        break;
    }
}

bool NetReader::fail() {
    ok_ = false;
    return false;
}

bool NetReader::u8(std::uint8_t& out) {
    if (!ok_ || offset_ >= data_.size()) {
        return fail();
    }
    out = static_cast<std::uint8_t>(data_[offset_++]);
    return true;
}

bool NetReader::u64(std::uint64_t& out) {
    out = 0;
    for (int i = 0; i < 8; ++i) {
        std::uint8_t byte = 0;
        if (!u8(byte)) {
            return false;
        }
        out |= static_cast<std::uint64_t>(byte) << (8 * i);
    }
    return true;
}

bool NetReader::varint(std::uint64_t& out) {
    out = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        std::uint8_t byte = 0;
        if (!u8(byte)) {
            return false;
        }
        if (shift == 63 && byte > 1) {
            return fail();
        }
        out |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return shift == 0 || byte != 0 ? true : fail();
        }
    }
    return fail();
}

bool NetReader::count(std::size_t& out, std::size_t max) {
    std::uint64_t raw = 0;
    if (!varint(raw) || raw > max) {
        return fail();
    }
    out = static_cast<std::size_t>(raw);
    return true;
}

bool NetReader::f32(float& out) {
    std::uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t byte = 0;
        if (!u8(byte)) {
            return false;
        }
        raw |= static_cast<std::uint32_t>(byte) << (8 * i);
    }
    std::memcpy(&out, &raw, sizeof(out));
    if (!std::isfinite(out)) {
        return fail();
    }
    return true;
}

bool NetReader::string(std::string& out, std::size_t maxBytes) {
    std::size_t size = 0;
    if (!count(size, maxBytes) || data_.size() - offset_ < size) {
        return fail();
    }
    out.assign(reinterpret_cast<const char*>(data_.data() + offset_), size);
    offset_ += size;
    return true;
}

bool NetReader::bits(std::vector<char>& out, std::size_t maxFlags) {
    std::size_t size = 0;
    if (!count(size, maxFlags)) {
        return false;
    }
    out.assign(size, 0);
    std::uint8_t pending = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (i % 8 == 0 && !u8(pending)) {
            return false;
        }
        out[i] = (pending >> (i % 8)) & 1 ? 1 : 0;
    }
    return true;
}

bool NetReader::value(NetValue& out, const NetType& type) {
    out = NetValue{};
    out.kind = type.kind;
    std::uint64_t raw = 0;
    switch (type.kind) {
    case NetTypeKind::Bool: {
        std::uint8_t flag = 0;
        if (!u8(flag) || flag > 1) {
            return fail();
        }
        out.flag = flag == 1;
        return true;
    }
    case NetTypeKind::Int:
        if (!varint(raw) || raw > static_cast<std::uint64_t>(type.hi - type.lo)) {
            return fail();
        }
        out.integer = type.lo + static_cast<std::int64_t>(raw);
        return true;
    case NetTypeKind::Real:
        if (!varint(raw) || raw > realStepCount(type)) {
            return fail();
        }
        out.real = type.realLo + static_cast<double>(raw) * type.step;
        return true;
    case NetTypeKind::Symbol:
        if (!varint(raw) || raw >= type.symbols.size()) {
            return fail();
        }
        out.text = type.symbols[static_cast<std::size_t>(raw)];
        return true;
    case NetTypeKind::Float: {
        float number = 0.0f;
        if (!f32(number)) {
            return false;
        }
        out.real = number;
        return true;
    }
    case NetTypeKind::String:
        return string(out.text, static_cast<std::size_t>(type.maxCount));
    case NetTypeKind::Entity:
        return string(out.text, kMaxNetEntityIdBytes);
    case NetTypeKind::List:
    case NetTypeKind::Table: {
        std::size_t entries = 0;
        if (!count(entries, static_cast<std::size_t>(type.maxCount))) {
            return false;
        }
        for (std::size_t i = 0; i < entries; ++i) {
            NetValue first;
            if (!value(first, type.items[0])) {
                return false;
            }
            out.items.push_back(std::move(first));
            if (type.kind == NetTypeKind::Table) {
                NetValue second;
                if (!value(second, type.items[1])) {
                    return false;
                }
                out.items.push_back(std::move(second));
            }
        }
        return true;
    }
    }
    return fail();
}

}
