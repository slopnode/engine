#pragma once

#include "net/net_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

class NetWriter {
public:
    void u8(std::uint8_t value);
    void u64(std::uint64_t value);
    void varint(std::uint64_t value);
    void f32(float value);
    void string(std::string_view value);
    void bits(const std::vector<char>& flags);
    void value(const NetValue& value, const NetType& type);

    const std::vector<std::byte>& data() const { return data_; }

private:
    std::vector<std::byte> data_;
};

class NetReader {
public:
    explicit NetReader(std::span<const std::byte> data) : data_(data) {}

    bool u8(std::uint8_t& out);
    bool u64(std::uint64_t& out);
    bool varint(std::uint64_t& out);
    bool count(std::size_t& out, std::size_t max);
    bool f32(float& out);
    bool string(std::string& out, std::size_t maxBytes);
    bool bits(std::vector<char>& out, std::size_t maxFlags);
    bool value(NetValue& out, const NetType& type);

    bool ok() const { return ok_; }
    bool atEnd() const { return ok_ && offset_ == data_.size(); }

private:
    bool fail();

    std::span<const std::byte> data_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

}
