#pragma once

#include "script/script_scope.hpp"

#include <raylib.h>
#include <s7.h>

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace slopengine {

class ArgReader {
public:
    ArgReader(s7_scheme* sc, const char* caller, s7_pointer args);

    s7_scheme* scheme() const { return sc_; }
    const char* caller() const { return caller_; }
    int index() const { return index_; }
    s7_pointer rest() const { return rest_; }
    bool more() const { return s7_is_pair(rest_); }
    s7_pointer peek() const;

    bool nextIsNumber() const;
    bool nextIsInteger() const;
    bool nextIsString() const;
    bool nextIsSymbol() const;
    bool nextIsName() const;
    bool nextIsBoolean() const;
    bool nextIsPair() const;
    bool nextIsList() const;

    s7_pointer any(const char* descr = "value");
    void skip();
    const char* string(const char* descr = "string");
    const char* name(const char* descr = "symbol or string");
    double real(const char* descr = "number");
    float realf(const char* descr = "number");
    int integer(const char* descr = "number");
    s7_int exactInteger(const char* descr = "integer");
    bool boolean(const char* descr = "boolean");
    bool flag(const char* descr = "boolean");
    bool truthy(const char* descr = "value");
    Vector3 vec3(const char* descr = "number");
    Vector3 vec3List(const char* descr = "(x y z) list");
    s7_pointer list(const char* descr = "list");
    s7_pointer alist(const char* descr = "alist");

    const char* optString(const char* fallback = nullptr);
    const char* optName(const char* fallback = nullptr);
    double optReal(double fallback);
    float optRealf(float fallback);
    int optInteger(int fallback);
    bool optBoolean(bool fallback);
    bool optFlag(bool fallback);
    s7_pointer optAny(s7_pointer fallback = nullptr);
    s7_pointer optList(s7_pointer fallback = nullptr);

    s7_pointer wrongType(const char* descr);
    s7_pointer wrongTypeAt(int index, s7_pointer value, const char* descr);

    bool ok() const { return error_ == nullptr; }
    s7_pointer error() const { return error_; }

private:
    s7_pointer take();
    s7_pointer requireNext(bool (*pred)(s7_pointer), const char* descr);

    s7_scheme* sc_;
    const char* caller_;
    s7_pointer rest_;
    int index_ = 1;
    s7_pointer error_ = nullptr;
};

bool readName(s7_pointer value, std::string& out);
bool readFlag(s7_scheme* sc, s7_pointer value, bool& out);
bool readReal(s7_scheme* sc, s7_pointer value, float& out);
bool readVec3(s7_scheme* sc, s7_pointer x, s7_pointer y, s7_pointer z, Vector3& out);
bool readVec3List(s7_scheme* sc, s7_pointer list, Vector3& out);

s7_pointer assocValue(s7_scheme* sc, s7_pointer alist, const char* key);
bool assocString(s7_scheme* sc, s7_pointer alist, const char* key, std::string& out);
bool assocReal(s7_scheme* sc, s7_pointer alist, const char* key, float& out);
bool assocReal(s7_scheme* sc, s7_pointer alist, const char* key, double& out);
bool assocInt(s7_scheme* sc, s7_pointer alist, const char* key, int& out);
bool assocBool(s7_scheme* sc, s7_pointer alist, const char* key, bool& out);
bool assocVec3(s7_scheme* sc, s7_pointer alist, const char* key, Vector3& out);

template <typename Apply>
s7_pointer rejectedAlistEntry(s7_pointer alist, Apply apply) {
    for (s7_pointer cursor = alist; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry)) ||
            !apply(s7_symbol_name(s7_car(entry)), s7_cdr(entry))) {
            return entry;
        }
    }
    return nullptr;
}

s7_pointer makeVec3List(s7_scheme* sc, Vector3 value);
s7_pointer makeVec3List(s7_scheme* sc, double x, double y, double z);
s7_pointer makeStringList(s7_scheme* sc, const std::vector<std::string>& values);
s7_pointer makeTaggedList(s7_scheme* sc, const char* tag, s7_pointer rest);

struct AlistEntry {
    const char* key;
    s7_pointer value;
};

s7_pointer makeAlist(s7_scheme* sc, std::initializer_list<AlistEntry> entries);

struct SchemeBinding {
    const char* name;
    s7_function fn;
    int required;
    int optional;
    bool rest;
    const char* doc;
    ScriptCap cap = ScriptCap::None;
};

void registerBindings(s7_scheme* sc, std::span<const SchemeBinding> bindings);

enum class ClauseArg {
    Any,
    Number,
    Name,
    Boolean,
};

struct ClauseSpec {
    const char* tag;
    int required;
    int optional;
    bool rest;
    const char* doc;
    ClauseArg arg = ClauseArg::Any;
};

void registerClauseBuilders(s7_scheme* sc, std::span<const ClauseSpec> clauses);

}
