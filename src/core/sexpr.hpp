#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace slopengine {

enum class SexprKind {
    Atom,
    String,
    Number,
    List,
};

struct Sexpr {
    SexprKind kind = SexprKind::Atom;
    std::string text;
    double number = 0.0;
    std::vector<Sexpr> list;
    int line = 1;
    int column = 1;

    bool isAtom(std::string_view name) const;
    bool isList() const { return kind == SexprKind::List; }
    bool isString() const { return kind == SexprKind::String; }
    bool isNumber() const { return kind == SexprKind::Number; }
};

struct SexprParseError {
    std::string message;
    int line = 1;
    int column = 1;
};

struct SexprParseResult {
    std::vector<Sexpr> forms;
    SexprParseError error{};
    bool ok = false;
};

SexprParseResult parseSexprs(std::string_view source);

std::string formatSexprError(std::string_view path, const SexprParseError& error);

}
