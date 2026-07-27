#include "map/handler_binding.hpp"

#include <cmath>
#include <cstring>
#include <sstream>

namespace slopengine {

namespace {

std::string escapeSchemeString(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string formatFloat(float value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

bool readString(s7_scheme* sc, s7_pointer value, std::string& out) {
    (void)sc;
    if (s7_is_string(value)) {
        out = s7_string(value);
        return true;
    }
    if (s7_is_symbol(value)) {
        out = s7_symbol_name(value);
        return true;
    }
    return false;
}

bool parseArgValue(
    s7_scheme* sc,
    HandlerArgType type,
    s7_pointer values,
    HandlerArgValue& out) {
    out = HandlerArgValue{};
    out.type = type;
    switch (type) {
    case HandlerArgType::Int:
        if (!s7_is_pair(values) || !s7_is_number(s7_car(values))) {
            return false;
        }
        out.i = static_cast<int>(s7_number_to_integer(sc, s7_car(values)));
        return true;
    case HandlerArgType::Float:
        if (!s7_is_pair(values) || !s7_is_number(s7_car(values))) {
            return false;
        }
        out.f = static_cast<float>(s7_number_to_real(sc, s7_car(values)));
        return true;
    case HandlerArgType::Bool:
        if (!s7_is_pair(values)) {
            return false;
        }
        if (s7_is_boolean(s7_car(values))) {
            out.b = s7_boolean(sc, s7_car(values));
            return true;
        }
        if (s7_is_integer(s7_car(values))) {
            out.b = s7_integer(s7_car(values)) != 0;
            return true;
        }
        return false;
    case HandlerArgType::String:
    case HandlerArgType::Thing:
    case HandlerArgType::Brush:
    case HandlerArgType::Face:
        if (!s7_is_pair(values)) {
            return false;
        }
        return readString(sc, s7_car(values), out.s);
    case HandlerArgType::Color:
    case HandlerArgType::Vec3:
        if (!s7_is_pair(values) || !s7_is_pair(s7_cdr(values)) ||
            !s7_is_pair(s7_cddr(values))) {
            return false;
        }
        if (!s7_is_number(s7_car(values)) || !s7_is_number(s7_cadr(values)) ||
            !s7_is_number(s7_caddr(values))) {
            return false;
        }
        out.v.x = static_cast<float>(s7_number_to_real(sc, s7_car(values)));
        out.v.y = static_cast<float>(s7_number_to_real(sc, s7_cadr(values)));
        out.v.z = static_cast<float>(s7_number_to_real(sc, s7_caddr(values)));
        return true;
    }
    return false;
}

bool inferArgType(s7_scheme* sc, s7_pointer values, HandlerArgType& out) {
    if (!s7_is_pair(values)) {
        return false;
    }
    const s7_pointer a = s7_car(values);
    const s7_pointer rest = s7_cdr(values);
    if (s7_is_boolean(a)) {
        out = HandlerArgType::Bool;
        return true;
    }
    if (s7_is_string(a) || s7_is_symbol(a)) {
        out = HandlerArgType::String;
        return true;
    }
    if (s7_is_number(a) && s7_is_pair(rest) && s7_is_pair(s7_cdr(rest)) &&
        s7_is_number(s7_car(rest)) && s7_is_number(s7_cadr(rest)) &&
        s7_is_null(sc, s7_cddr(rest))) {
        out = HandlerArgType::Vec3;
        return true;
    }
    if (s7_is_number(a) && s7_is_null(sc, rest)) {
        if (s7_is_integer(a) && !s7_is_real(a)) {
            out = HandlerArgType::Int;
        } else if (s7_is_integer(a) &&
                   std::floor(s7_number_to_real(sc, a)) == s7_number_to_real(sc, a)) {
            out = HandlerArgType::Int;
        } else {
            out = HandlerArgType::Float;
        }
        return true;
    }
    if (s7_is_number(a)) {
        out = HandlerArgType::Float;
        return true;
    }
    return false;
}

std::string formatArgClause(const HandlerArg& arg) {
    std::ostringstream out;
    out << "(" << arg.name;
    switch (arg.value.type) {
    case HandlerArgType::Int:
        out << " " << arg.value.i;
        break;
    case HandlerArgType::Float:
        out << " " << formatFloat(arg.value.f);
        break;
    case HandlerArgType::Bool:
        out << (arg.value.b ? " #t" : " #f");
        break;
    case HandlerArgType::String:
    case HandlerArgType::Thing:
    case HandlerArgType::Brush:
    case HandlerArgType::Face:
        out << " " << escapeSchemeString(arg.value.s);
        break;
    case HandlerArgType::Color:
    case HandlerArgType::Vec3:
        out << " " << formatFloat(arg.value.v.x) << " " << formatFloat(arg.value.v.y) << " "
            << formatFloat(arg.value.v.z);
        break;
    }
    out << ")";
    return out.str();
}

s7_pointer valueToScheme(s7_scheme* sc, const HandlerArgValue& value) {
    switch (value.type) {
    case HandlerArgType::Int:
        return s7_make_integer(sc, value.i);
    case HandlerArgType::Float:
        return s7_make_real(sc, value.f);
    case HandlerArgType::Bool:
        return s7_make_boolean(sc, value.b);
    case HandlerArgType::String:
    case HandlerArgType::Thing:
    case HandlerArgType::Brush:
    case HandlerArgType::Face:
        return s7_make_string(sc, value.s.c_str());
    case HandlerArgType::Color:
    case HandlerArgType::Vec3:
        return s7_list(
            sc,
            3,
            s7_make_real(sc, value.v.x),
            s7_make_real(sc, value.v.y),
            s7_make_real(sc, value.v.z));
    }
    return s7_f(sc);
}

} // namespace

bool parseHandlerArgTypeName(std::string_view name, HandlerArgType& out) {
    if (name == "int") {
        out = HandlerArgType::Int;
        return true;
    }
    if (name == "float") {
        out = HandlerArgType::Float;
        return true;
    }
    if (name == "bool") {
        out = HandlerArgType::Bool;
        return true;
    }
    if (name == "string") {
        out = HandlerArgType::String;
        return true;
    }
    if (name == "color") {
        out = HandlerArgType::Color;
        return true;
    }
    if (name == "vec3") {
        out = HandlerArgType::Vec3;
        return true;
    }
    if (name == "thing") {
        out = HandlerArgType::Thing;
        return true;
    }
    if (name == "brush") {
        out = HandlerArgType::Brush;
        return true;
    }
    if (name == "face") {
        out = HandlerArgType::Face;
        return true;
    }
    return false;
}

const char* handlerArgTypeName(HandlerArgType type) {
    switch (type) {
    case HandlerArgType::Int:
        return "int";
    case HandlerArgType::Float:
        return "float";
    case HandlerArgType::Bool:
        return "bool";
    case HandlerArgType::String:
        return "string";
    case HandlerArgType::Color:
        return "color";
    case HandlerArgType::Vec3:
        return "vec3";
    case HandlerArgType::Thing:
        return "thing";
    case HandlerArgType::Brush:
        return "brush";
    case HandlerArgType::Face:
        return "face";
    }
    return "string";
}

bool parseHandlerBinding(s7_scheme* sc, s7_pointer rest, HandlerBinding& out) {
    out.clear();
    if (!s7_is_pair(rest)) {
        return false;
    }
    if (!readString(sc, s7_car(rest), out.id) || out.id.empty()) {
        out.clear();
        return false;
    }

    for (s7_pointer cursor = s7_cdr(rest); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer clause = s7_car(cursor);
        if (!s7_is_pair(clause)) {
            continue;
        }
        std::string name;
        if (!readString(sc, s7_car(clause), name) || name.empty()) {
            continue;
        }
        const s7_pointer values = s7_cdr(clause);
        HandlerArgType type = HandlerArgType::String;
        if (!inferArgType(sc, values, type)) {
            continue;
        }
        // Prefer color tag name for 3-float vectors named color
        if (type == HandlerArgType::Vec3 && name == "color") {
            type = HandlerArgType::Color;
        }
        HandlerArg arg{};
        arg.name = std::move(name);
        if (!parseArgValue(sc, type, values, arg.value)) {
            continue;
        }
        out.args.push_back(std::move(arg));
    }
    return true;
}

std::string formatHandlerBindingClause(const char* tag, const HandlerBinding& binding) {
    std::ostringstream out;
    out << "(" << tag << " " << escapeSchemeString(binding.id);
    for (const HandlerArg& arg : binding.args) {
        out << " " << formatArgClause(arg);
    }
    out << ")";
    return out.str();
}

HandlerArgValue* findHandlerArg(HandlerBinding& binding, std::string_view name) {
    for (HandlerArg& arg : binding.args) {
        if (arg.name == name) {
            return &arg.value;
        }
    }
    return nullptr;
}

const HandlerArgValue* findHandlerArg(const HandlerBinding& binding, std::string_view name) {
    for (const HandlerArg& arg : binding.args) {
        if (arg.name == name) {
            return &arg.value;
        }
    }
    return nullptr;
}

s7_pointer handlerArgsToAlist(s7_scheme* sc, const std::vector<HandlerArg>& args) {
    s7_pointer alist = s7_nil(sc);
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        const s7_pointer pair =
            s7_cons(sc, s7_make_symbol(sc, it->name.c_str()), valueToScheme(sc, it->value));
        alist = s7_cons(sc, pair, alist);
    }
    return alist;
}

}
