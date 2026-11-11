#include "script/s7_read.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

namespace slopengine {

namespace {

bool isName(s7_pointer value) {
    return s7_is_string(value) || s7_is_symbol(value);
}

bool isFlag(s7_pointer value) {
    return s7_is_boolean(value) || s7_is_integer(value);
}

bool isAnything(s7_pointer) {
    return true;
}

const char* nameOf(s7_pointer value) {
    return s7_is_string(value) ? s7_string(value) : s7_symbol_name(value);
}

} // namespace

ArgReader::ArgReader(s7_scheme* sc, const char* caller, s7_pointer args)
    : sc_(sc), caller_(caller), rest_(args) {}

s7_pointer ArgReader::peek() const {
    return s7_is_pair(rest_) ? s7_car(rest_) : nullptr;
}

bool ArgReader::nextIsNumber() const {
    return more() && s7_is_number(s7_car(rest_));
}

bool ArgReader::nextIsInteger() const {
    return more() && s7_is_integer(s7_car(rest_));
}

bool ArgReader::nextIsString() const {
    return more() && s7_is_string(s7_car(rest_));
}

bool ArgReader::nextIsSymbol() const {
    return more() && s7_is_symbol(s7_car(rest_));
}

bool ArgReader::nextIsName() const {
    return more() && isName(s7_car(rest_));
}

bool ArgReader::nextIsBoolean() const {
    return more() && s7_is_boolean(s7_car(rest_));
}

bool ArgReader::nextIsPair() const {
    return more() && s7_is_pair(s7_car(rest_));
}

bool ArgReader::nextIsList() const {
    return more() && s7_is_list(sc_, s7_car(rest_));
}

s7_pointer ArgReader::take() {
    s7_pointer value = s7_car(rest_);
    rest_ = s7_cdr(rest_);
    ++index_;
    return value;
}

s7_pointer ArgReader::requireNext(bool (*pred)(s7_pointer), const char* descr) {
    if (!ok()) {
        return nullptr;
    }
    if (!more() || !pred(s7_car(rest_))) {
        wrongType(descr);
        return nullptr;
    }
    return take();
}

s7_pointer ArgReader::any(const char* descr) {
    s7_pointer value = requireNext(isAnything, descr);
    return value != nullptr ? value : s7_f(sc_);
}

void ArgReader::skip() {
    if (more()) {
        take();
    }
}

const char* ArgReader::string(const char* descr) {
    s7_pointer value = requireNext(s7_is_string, descr);
    return value != nullptr ? s7_string(value) : "";
}

const char* ArgReader::name(const char* descr) {
    s7_pointer value = requireNext(isName, descr);
    return value != nullptr ? nameOf(value) : "";
}

double ArgReader::real(const char* descr) {
    s7_pointer value = requireNext(s7_is_number, descr);
    return value != nullptr ? s7_number_to_real(sc_, value) : 0.0;
}

float ArgReader::realf(const char* descr) {
    return static_cast<float>(real(descr));
}

int ArgReader::integer(const char* descr) {
    return static_cast<int>(real(descr));
}

s7_int ArgReader::exactInteger(const char* descr) {
    s7_pointer value = requireNext(s7_is_integer, descr);
    return value != nullptr ? s7_integer(value) : 0;
}

bool ArgReader::boolean(const char* descr) {
    s7_pointer value = requireNext(s7_is_boolean, descr);
    return value != nullptr && s7_boolean(sc_, value);
}

bool ArgReader::flag(const char* descr) {
    s7_pointer value = requireNext(isFlag, descr);
    if (value == nullptr) {
        return false;
    }
    return s7_is_boolean(value) ? s7_boolean(sc_, value) : s7_integer(value) != 0;
}

bool ArgReader::truthy(const char* descr) {
    s7_pointer value = requireNext(isAnything, descr);
    return value != nullptr && value != s7_f(sc_);
}

Vector3 ArgReader::vec3(const char* descr) {
    Vector3 out{};
    out.x = realf(descr);
    out.y = realf(descr);
    out.z = realf(descr);
    return out;
}

Vector3 ArgReader::vec3List(const char* descr) {
    Vector3 out{};
    if (!ok()) {
        return out;
    }
    if (!more() || !readVec3List(sc_, s7_car(rest_), out)) {
        wrongType(descr);
        return out;
    }
    take();
    return out;
}

s7_pointer ArgReader::list(const char* descr) {
    if (!ok()) {
        return s7_nil(sc_);
    }
    if (!more() || !s7_is_list(sc_, s7_car(rest_))) {
        wrongType(descr);
        return s7_nil(sc_);
    }
    return take();
}

s7_pointer ArgReader::alist(const char* descr) {
    return list(descr);
}

const char* ArgReader::optString(const char* fallback) {
    return nextIsString() ? s7_string(take()) : fallback;
}

const char* ArgReader::optName(const char* fallback) {
    return nextIsName() ? nameOf(take()) : fallback;
}

double ArgReader::optReal(double fallback) {
    return nextIsNumber() ? s7_number_to_real(sc_, take()) : fallback;
}

float ArgReader::optRealf(float fallback) {
    return static_cast<float>(optReal(static_cast<double>(fallback)));
}

int ArgReader::optInteger(int fallback) {
    return nextIsNumber() ? static_cast<int>(s7_number_to_real(sc_, take())) : fallback;
}

bool ArgReader::optBoolean(bool fallback) {
    return nextIsBoolean() ? s7_boolean(sc_, take()) : fallback;
}

bool ArgReader::optFlag(bool fallback) {
    if (nextIsBoolean()) {
        return s7_boolean(sc_, take());
    }
    if (nextIsInteger()) {
        return s7_integer(take()) != 0;
    }
    return fallback;
}

s7_pointer ArgReader::optAny(s7_pointer fallback) {
    return more() ? take() : fallback;
}

s7_pointer ArgReader::optList(s7_pointer fallback) {
    return nextIsList() ? take() : fallback;
}

s7_pointer ArgReader::wrongType(const char* descr) {
    return wrongTypeAt(index_, more() ? s7_car(rest_) : rest_, descr);
}

s7_pointer ArgReader::wrongTypeAt(int index, s7_pointer value, const char* descr) {
    error_ = s7_wrong_type_arg_error(sc_, caller_, index, value, descr);
    return error_;
}

bool readName(s7_pointer value, std::string& out) {
    if (!isName(value)) {
        return false;
    }
    out = nameOf(value);
    return true;
}

bool readFlag(s7_scheme* sc, s7_pointer value, bool& out) {
    if (s7_is_boolean(value)) {
        out = s7_boolean(sc, value);
        return true;
    }
    if (s7_is_integer(value)) {
        out = s7_integer(value) != 0;
        return true;
    }
    return false;
}

bool readReal(s7_scheme* sc, s7_pointer value, float& out) {
    if (!s7_is_number(value)) {
        return false;
    }
    out = static_cast<float>(s7_number_to_real(sc, value));
    return true;
}

bool readVec3(s7_scheme* sc, s7_pointer x, s7_pointer y, s7_pointer z, Vector3& out) {
    if (!s7_is_number(x) || !s7_is_number(y) || !s7_is_number(z)) {
        return false;
    }
    out.x = static_cast<float>(s7_number_to_real(sc, x));
    out.y = static_cast<float>(s7_number_to_real(sc, y));
    out.z = static_cast<float>(s7_number_to_real(sc, z));
    return true;
}

bool readVec3List(s7_scheme* sc, s7_pointer list, Vector3& out) {
    if (!s7_is_pair(list) || !s7_is_pair(s7_cdr(list)) || !s7_is_pair(s7_cddr(list))) {
        return false;
    }
    return readVec3(sc, s7_car(list), s7_cadr(list), s7_caddr(list), out);
}

s7_pointer assocValue(s7_scheme* sc, s7_pointer alist, const char* key) {
    if (!s7_is_pair(alist) && !s7_is_null(sc, alist)) {
        return nullptr;
    }
    const s7_pointer pair = s7_assoc(sc, s7_make_symbol(sc, key), alist);
    return s7_is_pair(pair) ? s7_cdr(pair) : nullptr;
}

namespace {

s7_pointer assocScalar(s7_scheme* sc, s7_pointer alist, const char* key) {
    s7_pointer value = assocValue(sc, alist, key);
    if (value != nullptr && s7_is_pair(value)) {
        value = s7_car(value);
    }
    return value;
}

} // namespace

bool assocString(s7_scheme* sc, s7_pointer alist, const char* key, std::string& out) {
    s7_pointer value = assocScalar(sc, alist, key);
    return value != nullptr && readName(value, out);
}

bool assocReal(s7_scheme* sc, s7_pointer alist, const char* key, float& out) {
    s7_pointer value = assocScalar(sc, alist, key);
    return value != nullptr && readReal(sc, value, out);
}

bool assocReal(s7_scheme* sc, s7_pointer alist, const char* key, double& out) {
    s7_pointer value = assocScalar(sc, alist, key);
    if (value == nullptr || !s7_is_number(value)) {
        return false;
    }
    out = s7_number_to_real(sc, value);
    return true;
}

bool assocInt(s7_scheme* sc, s7_pointer alist, const char* key, int& out) {
    s7_pointer value = assocScalar(sc, alist, key);
    if (value == nullptr || !s7_is_integer(value)) {
        return false;
    }
    out = static_cast<int>(s7_integer(value));
    return true;
}

bool assocBool(s7_scheme* sc, s7_pointer alist, const char* key, bool& out) {
    s7_pointer value = assocScalar(sc, alist, key);
    return value != nullptr && readFlag(sc, value, out);
}

bool assocVec3(s7_scheme* sc, s7_pointer alist, const char* key, Vector3& out) {
    s7_pointer value = assocValue(sc, alist, key);
    if (value == nullptr) {
        return false;
    }
    if (s7_is_pair(value) && s7_is_pair(s7_car(value))) {
        value = s7_car(value);
    }
    return readVec3List(sc, value, out);
}

s7_pointer makeVec3List(s7_scheme* sc, Vector3 value) {
    return makeVec3List(
        sc,
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z));
}

s7_pointer makeVec3List(s7_scheme* sc, double x, double y, double z) {
    return s7_list(sc, 3, s7_make_real(sc, x), s7_make_real(sc, y), s7_make_real(sc, z));
}

s7_pointer makeStringList(s7_scheme* sc, const std::vector<std::string>& values) {
    s7_pointer list = s7_nil(sc);
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        list = s7_cons(sc, s7_make_string(sc, it->c_str()), list);
    }
    return list;
}

s7_pointer makeTaggedList(s7_scheme* sc, const char* tag, s7_pointer rest) {
    return s7_cons(sc, s7_make_symbol(sc, tag), rest);
}

s7_pointer makeAlist(s7_scheme* sc, std::initializer_list<AlistEntry> entries) {
    s7_pointer list = s7_nil(sc);
    for (auto it = std::rbegin(entries); it != std::rend(entries); ++it) {
        list = s7_cons(sc, s7_cons(sc, s7_make_symbol(sc, it->key), it->value), list);
    }
    return list;
}

namespace {

struct Closure {
    bool isClause = false;
    SchemeBinding binding{};
    ClauseSpec clause{};
};

constexpr std::size_t kClosureCapacity = 1024;
std::array<Closure, kClosureCapacity> g_closures{};
std::size_t g_closureCount = 0;

bool sameText(const char* a, const char* b) {
    return a == b || (a != nullptr && b != nullptr && std::strcmp(a, b) == 0);
}

bool sameClosure(const Closure& a, const Closure& b) {
    if (a.isClause != b.isClause) {
        return false;
    }
    if (a.isClause) {
        return sameText(a.clause.tag, b.clause.tag) && a.clause.required == b.clause.required &&
               a.clause.optional == b.clause.optional && a.clause.rest == b.clause.rest &&
               a.clause.arg == b.clause.arg;
    }
    return a.binding.fn == b.binding.fn && a.binding.cap == b.binding.cap &&
           sameText(a.binding.name, b.binding.name);
}

bool clauseArgAccepts(ClauseArg arg, s7_pointer value) {
    switch (arg) {
    case ClauseArg::Number:
        return s7_is_number(value);
    case ClauseArg::Name:
        return isName(value);
    case ClauseArg::Boolean:
        return s7_is_boolean(value);
    case ClauseArg::Any:
        break;
    }
    return true;
}

const char* clauseArgDescr(ClauseArg arg) {
    switch (arg) {
    case ClauseArg::Number:
        return "number";
    case ClauseArg::Name:
        return "symbol";
    case ClauseArg::Boolean:
        return "boolean";
    case ClauseArg::Any:
        break;
    }
    return "value";
}

s7_pointer buildClause(s7_scheme* sc, s7_pointer args, const ClauseSpec& spec) {
    s7_pointer cursor = args;
    for (int i = 0; i < spec.required; ++i) {
        if (!s7_is_pair(cursor)) {
            return s7_wrong_type_arg_error(sc, spec.tag, i + 1, args, spec.doc);
        }
        cursor = s7_cdr(cursor);
    }
    if (spec.arg != ClauseArg::Any && s7_is_pair(args) && !clauseArgAccepts(spec.arg, s7_car(args))) {
        return s7_wrong_type_arg_error(sc, spec.tag, 1, args, clauseArgDescr(spec.arg));
    }
    return makeTaggedList(sc, spec.tag, args);
}

s7_pointer callClosure(s7_scheme* sc, s7_pointer args, const Closure& closure) {
    if (closure.isClause) {
        return buildClause(sc, args, closure.clause);
    }
    if (closure.binding.cap != ScriptCap::None && !requireCap(sc, closure.binding.cap)) {
        return s7_f(sc);
    }
    return closure.binding.fn(sc, args);
}

template <std::size_t I>
s7_pointer closureTrampoline(s7_scheme* sc, s7_pointer args) {
    return callClosure(sc, args, g_closures[I]);
}

template <std::size_t... I>
constexpr std::array<s7_function, sizeof...(I)> trampolineTable(std::index_sequence<I...>) {
    return {{&closureTrampoline<I>...}};
}

const std::array<s7_function, kClosureCapacity> g_trampolines =
    trampolineTable(std::make_index_sequence<kClosureCapacity>{});

s7_function acquireClosure(const Closure& wanted, const char* label) {
    for (std::size_t i = 0; i < g_closureCount; ++i) {
        if (sameClosure(g_closures[i], wanted)) {
            return g_trampolines[i];
        }
    }
    if (g_closureCount >= kClosureCapacity) {
        TraceLog(LOG_FATAL, "s7_read: binding closure pool exhausted while registering '%s'", label);
        return nullptr;
    }
    g_closures[g_closureCount] = wanted;
    return g_trampolines[g_closureCount++];
}

} // namespace

void registerBindings(s7_scheme* sc, std::span<const SchemeBinding> bindings) {
    for (const SchemeBinding& binding : bindings) {
        s7_function fn = binding.fn;
        if (binding.cap != ScriptCap::None) {
            Closure closure{};
            closure.binding = binding;
            fn = acquireClosure(closure, binding.name);
        }
        s7_define_function(sc, binding.name, fn, binding.required, binding.optional, binding.rest, binding.doc);
    }
}

void registerClauseBuilders(s7_scheme* sc, std::span<const ClauseSpec> clauses) {
    for (const ClauseSpec& spec : clauses) {
        Closure closure{};
        closure.isClause = true;
        closure.clause = spec;
        s7_define_function(
            sc,
            spec.tag,
            acquireClosure(closure, spec.tag),
            spec.required,
            spec.optional,
            spec.rest,
            spec.doc);
    }
}

}
