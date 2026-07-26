#pragma once

#include <raylib.h>
#include <s7.h>

#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

enum class HandlerArgType {
    Int,
    Float,
    Bool,
    String,
    Color,
    Vec3,
    Thing,
    Brush,
    Face,
};

struct HandlerArgValue {
    HandlerArgType type = HandlerArgType::String;
    int i = 0;
    float f = 0.0f;
    bool b = false;
    std::string s;
    Vector3 v{};

    bool operator==(const HandlerArgValue& other) const {
        if (type != other.type) {
            return false;
        }
        switch (type) {
        case HandlerArgType::Int:
            return i == other.i;
        case HandlerArgType::Float:
            return f == other.f;
        case HandlerArgType::Bool:
            return b == other.b;
        case HandlerArgType::String:
        case HandlerArgType::Thing:
        case HandlerArgType::Brush:
        case HandlerArgType::Face:
            return s == other.s;
        case HandlerArgType::Color:
        case HandlerArgType::Vec3:
            return v.x == other.v.x && v.y == other.v.y && v.z == other.v.z;
        }
        return false;
    }
};

struct HandlerArg {
    std::string name;
    HandlerArgValue value;

    bool operator==(const HandlerArg& other) const {
        return name == other.name && value == other.value;
    }
};

struct HandlerBinding {
    std::string id;
    std::vector<HandlerArg> args;

    bool empty() const {
        return id.empty();
    }

    void clear() {
        id.clear();
        args.clear();
    }

    bool operator==(const HandlerBinding& other) const {
        return id == other.id && args == other.args;
    }
};

bool parseHandlerArgTypeName(std::string_view name, HandlerArgType& out);
const char* handlerArgTypeName(HandlerArgType type);

bool parseHandlerBinding(s7_scheme* sc, s7_pointer rest, HandlerBinding& out);
std::string formatHandlerBindingClause(const char* tag, const HandlerBinding& binding);

HandlerArgValue* findHandlerArg(HandlerBinding& binding, std::string_view name);
const HandlerArgValue* findHandlerArg(const HandlerBinding& binding, std::string_view name);

s7_pointer handlerArgsToAlist(s7_scheme* sc, const std::vector<HandlerArg>& args);

}
