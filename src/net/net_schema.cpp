#include "net/net_schema.hpp"

#include "assets/asset_store.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace slopengine {

namespace {

constexpr int kMaxNetCount = 65535;

NetSchema g_netSchema;
int g_gcPauseDepth = 0;

bool isSymbolNamed(s7_pointer value, const char* name) {
    return s7_is_symbol(value) && std::string_view(s7_symbol_name(value)) == name;
}

bool readInt(s7_pointer value, std::int64_t& out) {
    if (!s7_is_integer(value)) {
        return false;
    }
    out = s7_integer(value);
    return true;
}

bool readNumber(s7_scheme* sc, s7_pointer value, double& out) {
    if (!s7_is_number(value) || !s7_is_real(value)) {
        return false;
    }
    out = s7_number_to_real(sc, value);
    return true;
}

bool readCount(s7_pointer value, int& out) {
    std::int64_t count = 0;
    if (!readInt(value, count) || count <= 0 || count > kMaxNetCount) {
        return false;
    }
    out = static_cast<int>(count);
    return true;
}

std::vector<s7_pointer> listItems(s7_scheme* sc, s7_pointer list, bool& proper) {
    std::vector<s7_pointer> items;
    s7_pointer cursor = list;
    while (s7_is_pair(cursor)) {
        items.push_back(s7_car(cursor));
        cursor = s7_cdr(cursor);
    }
    proper = s7_is_null(sc, cursor);
    return items;
}

int bitsFor(std::uint64_t states) {
    int bits = 0;
    while (states > 1 && (std::uint64_t{1} << bits) < states) {
        ++bits;
    }
    return bits;
}

std::string formatReal(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

bool parseTypeExpr(s7_scheme* sc, s7_pointer expr, NetType& out, std::string& error) {
    if (isSymbolNamed(expr, "bool")) {
        out = NetType{};
        out.kind = NetTypeKind::Bool;
        return true;
    }
    if (isSymbolNamed(expr, "entity")) {
        out = NetType{};
        out.kind = NetTypeKind::Entity;
        return true;
    }
    if (isSymbolNamed(expr, "float")) {
        out = NetType{};
        out.kind = NetTypeKind::Float;
        return true;
    }
    if (!s7_is_pair(expr) || !s7_is_symbol(s7_car(expr))) {
        error = "type must be a symbol such as bool, or a list such as (int 0 100)";
        return false;
    }
    bool proper = false;
    const std::vector<s7_pointer> args = listItems(sc, s7_cdr(expr), proper);
    const std::string head = s7_symbol_name(s7_car(expr));
    out = NetType{};
    if (!proper) {
        error = "malformed " + head + " type";
        return false;
    }
    if ((head == "bool" || head == "entity") && args.empty()) {
        out.kind = head == "bool" ? NetTypeKind::Bool : NetTypeKind::Entity;
        return true;
    }
    if (head == "int") {
        out.kind = NetTypeKind::Int;
        if (args.size() != 2 || !readInt(args[0], out.lo) || !readInt(args[1], out.hi) || out.lo > out.hi) {
            error = "int needs two integers, lo then hi, with lo <= hi";
            return false;
        }
        return true;
    }
    if (head == "real") {
        out.kind = NetTypeKind::Real;
        if (args.size() != 3 || !readNumber(sc, args[0], out.realLo) || !readNumber(sc, args[1], out.realHi) ||
            !readNumber(sc, args[2], out.step) || out.realLo >= out.realHi || out.step <= 0.0) {
            error = "real needs lo, hi and step, with lo < hi and step > 0";
            return false;
        }
        return true;
    }
    if (head == "symbol") {
        out.kind = NetTypeKind::Symbol;
        for (s7_pointer arg : args) {
            if (!s7_is_symbol(arg)) {
                error = "symbol type takes symbols only";
                return false;
            }
            const std::string name = s7_symbol_name(arg);
            if (std::find(out.symbols.begin(), out.symbols.end(), name) != out.symbols.end()) {
                error = "symbol type repeats " + name;
                return false;
            }
            out.symbols.push_back(name);
        }
        if (out.symbols.empty()) {
            error = "symbol type needs at least one symbol";
            return false;
        }
        return true;
    }
    if (head == "string") {
        out.kind = NetTypeKind::String;
        if (args.size() != 1 || !readCount(args[0], out.maxCount)) {
            error = "string needs a maximum length in bytes";
            return false;
        }
        return true;
    }
    if (head == "list") {
        out.kind = NetTypeKind::List;
        NetType item;
        if (args.size() != 2 || !parseTypeExpr(sc, args[0], item, error)) {
            if (error.empty()) {
                error = "list needs an element type and a maximum count";
            }
            return false;
        }
        if (!readCount(args[1], out.maxCount)) {
            error = "list needs a maximum count";
            return false;
        }
        out.items.push_back(std::move(item));
        return true;
    }
    if (head == "table") {
        out.kind = NetTypeKind::Table;
        NetType key;
        NetType value;
        if (args.size() != 3 || !parseTypeExpr(sc, args[0], key, error) || !parseTypeExpr(sc, args[1], value, error)) {
            if (error.empty()) {
                error = "table needs a key type, a value type and a maximum count";
            }
            return false;
        }
        if (!readCount(args[2], out.maxCount)) {
            error = "table needs a maximum count";
            return false;
        }
        out.items.push_back(std::move(key));
        out.items.push_back(std::move(value));
        return true;
    }
    error = "unknown type " + head;
    return false;
}

bool parseScope(s7_pointer spec, NetScope& out) {
    if (isSymbolNamed(spec, "player")) {
        out = NetScope{NetScopeKind::Player, {}};
        return true;
    }
    if (isSymbolNamed(spec, "session")) {
        out = NetScope{NetScopeKind::Session, {}};
        return true;
    }
    if (s7_is_pair(spec) && isSymbolNamed(s7_car(spec), "thing") && s7_is_pair(s7_cdr(spec)) &&
        s7_is_symbol(s7_cadr(spec))) {
        out = NetScope{NetScopeKind::Thing, s7_symbol_name(s7_cadr(spec))};
        return true;
    }
    return false;
}

bool parseAudience(s7_pointer value, NetAudience& out) {
    if (isSymbolNamed(value, "all")) {
        out = NetAudience::All;
    } else if (isSymbolNamed(value, "owner")) {
        out = NetAudience::Owner;
    } else if (isSymbolNamed(value, "none")) {
        out = NetAudience::None;
    } else {
        return false;
    }
    return true;
}

bool parseField(s7_scheme* sc, s7_pointer entry, const NetScope& scope, NetField& out, std::string& error) {
    if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
        error = "field entries look like (name (type ...) ...)";
        return false;
    }
    out = NetField{};
    out.name = s7_symbol_name(s7_car(entry));
    out.scope = scope;
    bool haveType = false;
    bool haveSave = false;
    s7_pointer defaultValue = nullptr;
    for (s7_pointer cursor = s7_cdr(entry); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer option = s7_car(cursor);
        if (!s7_is_pair(option) || !s7_is_symbol(s7_car(option))) {
            error = out.name + ": options look like (type ...), (default v), (send all), (travel #t), (save #t)";
            return false;
        }
        const std::string key = s7_symbol_name(s7_car(option));
        const s7_pointer rest = s7_cdr(option);
        if (key == "type") {
            const s7_pointer expr = s7_is_pair(rest) && s7_is_null(sc, s7_cdr(rest)) ? s7_car(rest) : rest;
            if (!parseTypeExpr(sc, expr, out.type, error)) {
                error = out.name + ": " + error;
                return false;
            }
            haveType = true;
        } else if (key == "default") {
            if (!s7_is_pair(rest)) {
                error = out.name + ": default needs a value";
                return false;
            }
            defaultValue = s7_car(rest);
        } else if (key == "send") {
            if (!s7_is_pair(rest) || !parseAudience(s7_car(rest), out.send)) {
                error = out.name + ": send is all, owner or none";
                return false;
            }
        } else if (key == "travel" || key == "save") {
            if (!s7_is_pair(rest) || !s7_is_boolean(s7_car(rest))) {
                error = out.name + ": " + key + " is #t or #f";
                return false;
            }
            const bool flag = s7_boolean(sc, s7_car(rest));
            if (key == "travel") {
                out.travel = flag;
            } else {
                out.save = flag;
                haveSave = true;
            }
        } else {
            error = out.name + ": unknown option " + key;
            return false;
        }
    }
    if (!haveType) {
        error = out.name + ": missing (type ...)";
        return false;
    }
    if (!haveSave) {
        out.save = out.travel;
    }
    out.defaultValue = netZeroValue(out.type);
    if (defaultValue != nullptr && !netValueFromScheme(sc, defaultValue, out.type, out.defaultValue, error)) {
        error = out.name + ": default " + error;
        return false;
    }
    return true;
}

NetBudget parseBudget(s7_pointer options) {
    NetBudget budget{};
    for (s7_pointer cursor = options; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer option = s7_car(cursor);
        if (!s7_is_pair(option) || !s7_is_pair(s7_cdr(option)) || !s7_is_integer(s7_cadr(option))) {
            continue;
        }
        const int amount = static_cast<int>(std::max<s7_int>(0, s7_integer(s7_cadr(option))));
        if (isSymbolNamed(s7_car(option), "fields") || isSymbolNamed(s7_car(option), "count")) {
            budget.fields = amount;
        } else if (isSymbolNamed(s7_car(option), "bytes")) {
            budget.bytes = amount;
        }
    }
    return budget;
}

std::string canonicalValue(const NetValue& value) {
    switch (value.kind) {
    case NetTypeKind::Bool:
        return value.flag ? "#t" : "#f";
    case NetTypeKind::Int:
        return std::to_string(value.integer);
    case NetTypeKind::Real:
    case NetTypeKind::Float:
        return formatReal(value.real);
    case NetTypeKind::Symbol:
    case NetTypeKind::String:
    case NetTypeKind::Entity:
        return "\"" + value.text + "\"";
    case NetTypeKind::List:
    case NetTypeKind::Table: {
        std::string out = "(";
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            out += (i == 0 ? "" : " ") + canonicalValue(value.items[i]);
        }
        return out + ")";
    }
    }
    return {};
}

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

double quantize(const NetType& type, double value) {
    const double steps = std::round((value - type.realLo) / type.step);
    return std::min(type.realHi, type.realLo + steps * type.step);
}

s7_pointer buildScheme(s7_scheme* sc, const NetValue& value) {
    switch (value.kind) {
    case NetTypeKind::Bool:
        return s7_make_boolean(sc, value.flag);
    case NetTypeKind::Int:
        return s7_make_integer(sc, value.integer);
    case NetTypeKind::Real:
    case NetTypeKind::Float:
        return s7_make_real(sc, value.real);
    case NetTypeKind::Symbol:
        return s7_make_symbol(sc, value.text.c_str());
    case NetTypeKind::String:
        return s7_make_string(sc, value.text.c_str());
    case NetTypeKind::Entity:
        return value.text.empty() ? s7_f(sc) : s7_make_string(sc, value.text.c_str());
    case NetTypeKind::List: {
        s7_pointer list = s7_nil(sc);
        for (auto it = value.items.rbegin(); it != value.items.rend(); ++it) {
            list = s7_cons(sc, buildScheme(sc, *it), list);
        }
        return list;
    }
    case NetTypeKind::Table: {
        s7_pointer list = s7_nil(sc);
        for (std::size_t i = value.items.size(); i >= 2; i -= 2) {
            list = s7_cons(
                sc, s7_cons(sc, buildScheme(sc, value.items[i - 2]), buildScheme(sc, value.items[i - 1])), list);
        }
        return list;
    }
    }
    return s7_f(sc);
}

} // namespace

void NetSchema::clear() {
    *this = NetSchema{};
}

void NetSchema::setExtension(s7_scheme*, s7_pointer extension) {
    for (s7_pointer cursor = extension; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            continue;
        }
        const NetBudget budget = parseBudget(s7_cdr(entry));
        if (isSymbolNamed(s7_car(entry), "player")) {
            playerBudget_ = budget;
        } else if (isSymbolNamed(s7_car(entry), "session")) {
            sessionBudget_ = budget;
        } else if (isSymbolNamed(s7_car(entry), "thing")) {
            thingBudget_ = budget;
        } else if (isSymbolNamed(s7_car(entry), "events")) {
            eventBudget_ = budget;
        }
    }
}

const NetBudget* NetSchema::budgetFor(NetScopeKind kind) const {
    switch (kind) {
    case NetScopeKind::Player:
        return &playerBudget_;
    case NetScopeKind::Session:
        return &sessionBudget_;
    case NetScopeKind::Thing:
        return &thingBudget_;
    }
    return nullptr;
}

NetBudget* NetSchema::usedFor(NetScopeKind kind) {
    switch (kind) {
    case NetScopeKind::Player:
        return &playerUsed_;
    case NetScopeKind::Session:
        return &sessionUsed_;
    case NetScopeKind::Thing:
        return &thingUsed_;
    }
    return nullptr;
}

bool NetSchema::addPackageFields(
    s7_scheme* sc,
    s7_pointer state,
    std::string_view packageId,
    PackageRole role,
    std::vector<std::string>& messages) {
    if (state == nullptr || s7_is_null(sc, state) || state == s7_undefined(sc)) {
        return true;
    }
    bool proper = false;
    const std::vector<s7_pointer> groups = listItems(sc, state, proper);
    if (!proper) {
        messages.push_back(std::string(packageId) + ": *net-state* must be a list of scopes");
        return false;
    }
    std::vector<NetField> pending;
    for (s7_pointer group : groups) {
        NetScope scope;
        if (!s7_is_pair(group) || !parseScope(s7_car(group), scope)) {
            messages.push_back(std::string(packageId) + ": scopes are player, session or (thing tag)");
            continue;
        }
        for (s7_pointer cursor = s7_cdr(group); s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            NetField field;
            std::string error;
            if (!parseField(sc, s7_car(cursor), scope, field, error)) {
                messages.push_back(std::string(packageId) + ": " + error);
                continue;
            }
            if (role == PackageRole::Mod) {
                field.name = std::string(packageId) + "/" + field.name;
            }
            field.packageId = std::string(packageId);
            field.role = role;
            const auto sameField = [&](const NetField& other) {
                return other.scope == field.scope && other.name == field.name;
            };
            if (std::any_of(fields_.begin(), fields_.end(), sameField) ||
                std::any_of(pending.begin(), pending.end(), sameField)) {
                messages.push_back(std::string(packageId) + ": " + field.name + " is declared twice in " +
                                   netScopeText(field.scope));
                continue;
            }
            pending.push_back(std::move(field));
        }
    }
    if (role == PackageRole::Mod && !pending.empty()) {
        NetBudget wanted[3]{};
        for (const NetField& field : pending) {
            NetBudget& budget = wanted[static_cast<int>(field.scope.kind)];
            budget.fields += 1;
            budget.bytes += (netTypeBits(field.type) + 7) / 8;
        }
        for (int kind = 0; kind < 3; ++kind) {
            if (wanted[kind].fields == 0) {
                continue;
            }
            const NetScopeKind scopeKind = static_cast<NetScopeKind>(kind);
            const NetBudget& limit = *budgetFor(scopeKind);
            const NetBudget& used = *usedFor(scopeKind);
            if (used.fields + wanted[kind].fields > limit.fields || used.bytes + wanted[kind].bytes > limit.bytes) {
                const char* scopeName = kind == 0 ? "player" : (kind == 1 ? "session" : "thing");
                char message[256];
                std::snprintf(
                    message,
                    sizeof(message),
                    "%.*s: refused, its %s fields need %d fields and %d bytes but the base package leaves %d and %d",
                    static_cast<int>(packageId.size()),
                    packageId.data(),
                    scopeName,
                    wanted[kind].fields,
                    wanted[kind].bytes,
                    std::max(0, limit.fields - used.fields),
                    std::max(0, limit.bytes - used.bytes));
                messages.push_back(message);
                return false;
            }
        }
        for (int kind = 0; kind < 3; ++kind) {
            NetBudget& used = *usedFor(static_cast<NetScopeKind>(kind));
            used.fields += wanted[kind].fields;
            used.bytes += wanted[kind].bytes;
        }
    }
    for (NetField& field : pending) {
        fields_.push_back(std::move(field));
    }
    return true;
}

bool NetSchema::addSessionKinds(
    s7_scheme* sc,
    s7_pointer kinds,
    std::string_view packageId,
    std::vector<std::string>& messages) {
    if (kinds == nullptr || s7_is_null(sc, kinds) || kinds == s7_undefined(sc)) {
        return true;
    }
    bool proper = false;
    const std::vector<s7_pointer> entries = listItems(sc, kinds, proper);
    const std::string package(packageId);
    if (!proper) {
        messages.push_back(package + ": *net-sessions* must be a list of session kinds");
        return false;
    }
    static constexpr const char* kLaterOptions[] = {"join", "late-join", "roles", "announce", "listing"};
    bool ok = true;
    for (s7_pointer entry : entries) {
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            messages.push_back(package + ": session kinds look like (name (players 1 8) ...)");
            ok = false;
            continue;
        }
        NetSessionKind kind;
        kind.name = s7_symbol_name(s7_car(entry));
        std::string error;
        for (s7_pointer cursor = s7_cdr(entry); error.empty() && s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            const s7_pointer option = s7_car(cursor);
            if (!s7_is_pair(option) || !s7_is_symbol(s7_car(option))) {
                error = "options look like (players 1 8)";
                break;
            }
            const std::string key = s7_symbol_name(s7_car(option));
            if (key == "players") {
                std::int64_t low = 0;
                std::int64_t high = 0;
                const s7_pointer rest = s7_cdr(option);
                if (!s7_is_pair(rest) || !s7_is_pair(s7_cdr(rest)) || !readInt(s7_car(rest), low) ||
                    !readInt(s7_cadr(rest), high) || low < 1 || high < low || high > kMaxNetPlayers) {
                    error = "players needs a minimum and a maximum between 1 and " + std::to_string(kMaxNetPlayers);
                    break;
                }
                kind.minPlayers = static_cast<int>(low);
                kind.maxPlayers = static_cast<int>(high);
            } else if (std::find_if(std::begin(kLaterOptions), std::end(kLaterOptions), [&](const char* name) {
                           return key == name;
                       }) == std::end(kLaterOptions)) {
                error = "unknown option " + key;
            }
        }
        if (error.empty() && findSessionKind(kind.name) != nullptr) {
            error = "is declared twice";
        }
        if (!error.empty()) {
            messages.push_back(package + ": session kind " + kind.name + " " + error);
            ok = false;
            continue;
        }
        sessionKinds_.push_back(std::move(kind));
    }
    return ok;
}

const NetSessionKind* NetSchema::findSessionKind(std::string_view name) const {
    for (const NetSessionKind& kind : sessionKinds_) {
        if (kind.name == name) {
            return &kind;
        }
    }
    return nullptr;
}

bool NetSchema::addPackageEvents(
    s7_scheme* sc,
    s7_pointer events,
    std::string_view packageId,
    PackageRole role,
    std::vector<std::string>& messages) {
    if (events == nullptr || s7_is_null(sc, events) || events == s7_undefined(sc)) {
        return true;
    }
    bool proper = false;
    const std::vector<s7_pointer> entries = listItems(sc, events, proper);
    const std::string package(packageId);
    if (!proper) {
        messages.push_back(package + ": *net-events* must be a list of events");
        return false;
    }
    std::vector<NetEventDef> pending;
    for (s7_pointer entry : entries) {
        if (!s7_is_pair(entry) || !s7_is_symbol(s7_car(entry))) {
            messages.push_back(package + ": events look like (name (args ...) (to all) (reliable #t))");
            continue;
        }
        NetEventDef def;
        def.name = s7_symbol_name(s7_car(entry));
        if (role == PackageRole::Mod) {
            def.name = package + "/" + def.name;
        }
        def.packageId = package;
        def.role = role;
        std::string error;
        for (s7_pointer cursor = s7_cdr(entry); error.empty() && s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
            const s7_pointer option = s7_car(cursor);
            if (!s7_is_pair(option) || !s7_is_symbol(s7_car(option))) {
                error = "options look like (args ...), (to all), (reliable #t)";
                break;
            }
            const std::string key = s7_symbol_name(s7_car(option));
            const s7_pointer rest = s7_cdr(option);
            if (key == "args") {
                for (s7_pointer arg = rest; s7_is_pair(arg); arg = s7_cdr(arg)) {
                    const s7_pointer spec = s7_car(arg);
                    if (!s7_is_pair(spec) || !s7_is_symbol(s7_car(spec)) || !s7_is_pair(s7_cdr(spec))) {
                        error = "arguments look like (name type)";
                        break;
                    }
                    NetEventArg parsed;
                    parsed.name = s7_symbol_name(s7_car(spec));
                    const s7_pointer typeRest = s7_cdr(spec);
                    const s7_pointer expr = s7_is_null(sc, s7_cdr(typeRest)) ? s7_car(typeRest) : typeRest;
                    if (!parseTypeExpr(sc, expr, parsed.type, error)) {
                        error = parsed.name + ": " + error;
                        break;
                    }
                    def.args.push_back(std::move(parsed));
                }
            } else if (key == "to") {
                const s7_pointer value = s7_is_pair(rest) ? s7_car(rest) : s7_nil(sc);
                if (isSymbolNamed(value, "all")) {
                    def.to = NetEventAudience::All;
                } else if (isSymbolNamed(value, "owner")) {
                    def.to = NetEventAudience::Owner;
                } else if (isSymbolNamed(value, "pvs")) {
                    def.to = NetEventAudience::Pvs;
                } else if (double radius = 0.0; s7_is_pair(value) && isSymbolNamed(s7_car(value), "near") &&
                           s7_is_pair(s7_cdr(value)) && readNumber(sc, s7_cadr(value), radius) && radius > 0.0) {
                    def.to = NetEventAudience::Near;
                    def.nearRadius = static_cast<float>(radius);
                } else {
                    error = "to is all, owner, pvs or (near metres)";
                }
            } else if (key == "reliable") {
                if (!s7_is_pair(rest) || !s7_is_boolean(s7_car(rest))) {
                    error = "reliable is #t or #f";
                } else {
                    def.reliable = s7_boolean(sc, s7_car(rest));
                }
            } else {
                error = "unknown option " + key;
            }
        }
        if (!error.empty()) {
            messages.push_back(package + ": " + def.name + ": " + error);
            continue;
        }
        const auto sameName = [&](const NetEventDef& other) { return other.name == def.name; };
        if (std::any_of(events_.begin(), events_.end(), sameName) || std::any_of(pending.begin(), pending.end(), sameName)) {
            messages.push_back(package + ": event " + def.name + " is declared twice");
            continue;
        }
        pending.push_back(std::move(def));
    }
    if (role == PackageRole::Mod && !pending.empty()) {
        NetBudget wanted{};
        for (const NetEventDef& def : pending) {
            int bits = 8;
            for (const NetEventArg& arg : def.args) {
                bits += netTypeBits(arg.type);
            }
            wanted.fields += 1;
            wanted.bytes += (bits + 7) / 8;
        }
        if (eventUsed_.fields + wanted.fields > eventBudget_.fields || eventUsed_.bytes + wanted.bytes > eventBudget_.bytes) {
            messages.push_back(package + ": refused, its events need " + std::to_string(wanted.fields) + " events and " +
                               std::to_string(wanted.bytes) + " bytes but the base package leaves " +
                               std::to_string(std::max(0, eventBudget_.fields - eventUsed_.fields)) + " and " +
                               std::to_string(std::max(0, eventBudget_.bytes - eventUsed_.bytes)));
            return false;
        }
        eventUsed_.fields += wanted.fields;
        eventUsed_.bytes += wanted.bytes;
    }
    for (NetEventDef& def : pending) {
        events_.push_back(std::move(def));
    }
    return true;
}

const NetEventDef* NetSchema::findEvent(std::string_view name) const {
    for (const NetEventDef& def : events_) {
        if (def.name == name) {
            return &def;
        }
    }
    return nullptr;
}

void NetSchema::finalize() {
    std::sort(fields_.begin(), fields_.end(), [](const NetField& a, const NetField& b) {
        if (a.packageId != b.packageId) {
            return a.packageId < b.packageId;
        }
        const std::string scopeA = netScopeText(a.scope);
        const std::string scopeB = netScopeText(b.scope);
        if (scopeA != scopeB) {
            return scopeA < scopeB;
        }
        return a.name < b.name;
    });
    std::map<std::string, int> slots;
    std::string canonical;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        NetField& field = fields_[i];
        field.id = static_cast<int>(i);
        field.slot = slots[netScopeText(field.scope)]++;
        canonical += field.packageId + "|" + netScopeText(field.scope) + "|" + field.name + "|" +
                     netTypeText(field.type) + "|" + canonicalValue(field.defaultValue) + "|" +
                     std::to_string(static_cast<int>(field.send)) + "|" + (field.travel ? "t" : "f") +
                     (field.save ? "t" : "f") + "\n";
    }
    std::sort(events_.begin(), events_.end(), [](const NetEventDef& a, const NetEventDef& b) {
        return a.packageId != b.packageId ? a.packageId < b.packageId : a.name < b.name;
    });
    for (std::size_t i = 0; i < events_.size(); ++i) {
        NetEventDef& def = events_[i];
        def.id = static_cast<int>(i);
        canonical += "event|" + def.packageId + "|" + def.name + "|" + std::to_string(static_cast<int>(def.to)) + "|" +
                     formatReal(def.nearRadius) + "|" + (def.reliable ? "r" : "u");
        for (const NetEventArg& arg : def.args) {
            canonical += "|" + arg.name + ":" + netTypeText(arg.type);
        }
        canonical += "\n";
    }
    hash_ = fnv1a(canonical);
}

const NetField* NetSchema::find(const NetScope& scope, std::string_view name) const {
    for (const NetField& field : fields_) {
        if (field.scope == scope && field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

std::vector<const NetField*> NetSchema::fieldsIn(const NetScope& scope) const {
    std::vector<const NetField*> out;
    for (const NetField& field : fields_) {
        if (field.scope == scope) {
            out.push_back(&field);
        }
    }
    return out;
}

NetSchema& netSchema() {
    return g_netSchema;
}

bool parseNetType(s7_scheme* sc, s7_pointer spec, NetType& out, std::string& error) {
    return parseTypeExpr(sc, spec, out, error);
}

int netTypeBits(const NetType& type) {
    switch (type.kind) {
    case NetTypeKind::Bool:
        return 1;
    case NetTypeKind::Int:
        return bitsFor(static_cast<std::uint64_t>(type.hi - type.lo) + 1);
    case NetTypeKind::Real:
        return bitsFor(static_cast<std::uint64_t>(std::llround((type.realHi - type.realLo) / type.step)) + 1);
    case NetTypeKind::Symbol:
        return bitsFor(type.symbols.size());
    case NetTypeKind::String:
        return bitsFor(static_cast<std::uint64_t>(type.maxCount) + 1) + 8 * type.maxCount;
    case NetTypeKind::Entity:
    case NetTypeKind::Float:
        return 32;
    case NetTypeKind::List:
        return bitsFor(static_cast<std::uint64_t>(type.maxCount) + 1) + type.maxCount * netTypeBits(type.items[0]);
    case NetTypeKind::Table:
        return bitsFor(static_cast<std::uint64_t>(type.maxCount) + 1) +
            type.maxCount * (netTypeBits(type.items[0]) + netTypeBits(type.items[1]));
    }
    return 0;
}

std::string netTypeText(const NetType& type) {
    switch (type.kind) {
    case NetTypeKind::Bool:
        return "bool";
    case NetTypeKind::Int:
        return "(int " + std::to_string(type.lo) + " " + std::to_string(type.hi) + ")";
    case NetTypeKind::Real:
        return "(real " + formatReal(type.realLo) + " " + formatReal(type.realHi) + " " + formatReal(type.step) + ")";
    case NetTypeKind::Symbol: {
        std::string out = "(symbol";
        for (const std::string& symbol : type.symbols) {
            out += " " + symbol;
        }
        return out + ")";
    }
    case NetTypeKind::String:
        return "(string " + std::to_string(type.maxCount) + ")";
    case NetTypeKind::Entity:
        return "entity";
    case NetTypeKind::Float:
        return "float";
    case NetTypeKind::List:
        return "(list " + netTypeText(type.items[0]) + " " + std::to_string(type.maxCount) + ")";
    case NetTypeKind::Table:
        return "(table " + netTypeText(type.items[0]) + " " + netTypeText(type.items[1]) + " " +
            std::to_string(type.maxCount) + ")";
    }
    return {};
}

std::string netScopeText(const NetScope& scope) {
    switch (scope.kind) {
    case NetScopeKind::Player:
        return "player";
    case NetScopeKind::Session:
        return "session";
    case NetScopeKind::Thing:
        return "(thing " + scope.tag + ")";
    }
    return {};
}

NetValue netZeroValue(const NetType& type) {
    NetValue value;
    value.kind = type.kind;
    switch (type.kind) {
    case NetTypeKind::Int:
        value.integer = std::clamp<std::int64_t>(0, type.lo, type.hi);
        break;
    case NetTypeKind::Real:
        value.real = quantize(type, std::clamp(0.0, type.realLo, type.realHi));
        break;
    case NetTypeKind::Symbol:
        value.text = type.symbols.front();
        break;
    default:
        break;
    }
    return value;
}

bool netValueFromScheme(s7_scheme* sc, s7_pointer value, const NetType& type, NetValue& out, std::string& error) {
    out = NetValue{};
    out.kind = type.kind;
    switch (type.kind) {
    case NetTypeKind::Bool:
        if (!s7_is_boolean(value)) {
            error = "expects #t or #f";
            return false;
        }
        out.flag = s7_boolean(sc, value);
        return true;
    case NetTypeKind::Int: {
        if (s7_is_integer(value)) {
            out.integer = s7_integer(value);
        } else if (s7_is_number(value) && s7_is_real(value)) {
            out.integer = std::llround(s7_number_to_real(sc, value));
        } else {
            error = "expects an integer";
            return false;
        }
        if (out.integer < type.lo || out.integer > type.hi) {
            error = std::to_string(out.integer) + " is outside " + netTypeText(type);
            return false;
        }
        return true;
    }
    case NetTypeKind::Real: {
        double number = 0.0;
        if (!readNumber(sc, value, number)) {
            error = "expects a number";
            return false;
        }
        if (number < type.realLo || number > type.realHi) {
            error = formatReal(number) + " is outside " + netTypeText(type);
            return false;
        }
        out.real = quantize(type, number);
        return true;
    }
    case NetTypeKind::Symbol: {
        if (!s7_is_symbol(value) ||
            std::find(type.symbols.begin(), type.symbols.end(), s7_symbol_name(value)) == type.symbols.end()) {
            error = "expects one of " + netTypeText(type);
            return false;
        }
        out.text = s7_symbol_name(value);
        return true;
    }
    case NetTypeKind::String: {
        if (!s7_is_string(value)) {
            error = "expects a string";
            return false;
        }
        out.text = s7_string(value);
        if (static_cast<int>(out.text.size()) > type.maxCount) {
            error = "string is longer than " + std::to_string(type.maxCount) + " bytes";
            return false;
        }
        return true;
    }
    case NetTypeKind::Float: {
        double number = 0.0;
        if (!readNumber(sc, value, number) || !std::isfinite(number) || std::fabs(number) > 3.0e38) {
            error = "expects a finite number";
            return false;
        }
        out.real = static_cast<float>(number);
        return true;
    }
    case NetTypeKind::Entity:
        if (value == s7_f(sc)) {
            return true;
        }
        if (!s7_is_string(value)) {
            error = "expects an entity id string or #f";
            return false;
        }
        out.text = s7_string(value);
        return true;
    case NetTypeKind::List:
    case NetTypeKind::Table: {
        bool proper = false;
        const std::vector<s7_pointer> items = listItems(sc, value, proper);
        if (!proper) {
            error = type.kind == NetTypeKind::List ? "expects a list" : "expects an alist";
            return false;
        }
        if (static_cast<int>(items.size()) > type.maxCount) {
            error = "has " + std::to_string(items.size()) + " entries, more than " + std::to_string(type.maxCount);
            return false;
        }
        for (s7_pointer item : items) {
            if (type.kind == NetTypeKind::List) {
                NetValue element;
                if (!netValueFromScheme(sc, item, type.items[0], element, error)) {
                    error = "element " + error;
                    return false;
                }
                out.items.push_back(std::move(element));
                continue;
            }
            if (!s7_is_pair(item)) {
                error = "expects (key . value) pairs";
                return false;
            }
            NetValue key;
            NetValue entry;
            if (!netValueFromScheme(sc, s7_car(item), type.items[0], key, error) ||
                !netValueFromScheme(sc, s7_cdr(item), type.items[1], entry, error)) {
                error = "entry " + error;
                return false;
            }
            out.items.push_back(std::move(key));
            out.items.push_back(std::move(entry));
        }
        return true;
    }
    }
    return false;
}

NetGcPause::NetGcPause(s7_scheme* sc) : sc_(sc) {
    if (g_gcPauseDepth++ == 0) {
        s7_gc_on(sc_, false);
    }
}

NetGcPause::~NetGcPause() {
    if (--g_gcPauseDepth == 0) {
        s7_gc_on(sc_, true);
    }
}

s7_pointer netValueToScheme(s7_scheme* sc, const NetValue& value) {
    NetGcPause pause(sc);
    return buildScheme(sc, value);
}

void loadPackageNetSchema(s7_scheme* sc, AssetStore& assets) {
    NetSchema& schema = netSchema();
    schema.clear();
    if (sc == nullptr) {
        return;
    }
    const auto loadOne = [&](const Package& package) {
        s7_define(sc, s7_rootlet(sc), s7_make_symbol(sc, "*net-state*"), s7_nil(sc));
        s7_define(sc, s7_rootlet(sc), s7_make_symbol(sc, "*net-extension*"), s7_nil(sc));
        s7_define(sc, s7_rootlet(sc), s7_make_symbol(sc, "*net-events*"), s7_nil(sc));
        s7_define(sc, s7_rootlet(sc), s7_make_symbol(sc, "*net-sessions*"), s7_nil(sc));
        if (!assets.loadDataFromPackage(sc, package.meta().id, "net")) {
            return;
        }
        std::vector<std::string> messages;
        if (package.role() != PackageRole::Mod) {
            schema.setExtension(sc, s7_name_to_value(sc, "*net-extension*"));
            schema.addSessionKinds(sc, s7_name_to_value(sc, "*net-sessions*"), package.meta().id, messages);
        } else if (!s7_is_null(sc, s7_name_to_value(sc, "*net-sessions*"))) {
            messages.push_back(package.meta().id + ": mods can't declare *net-sessions*");
        }
        schema.addPackageFields(sc, s7_name_to_value(sc, "*net-state*"), package.meta().id, package.role(), messages);
        schema.addPackageEvents(sc, s7_name_to_value(sc, "*net-events*"), package.meta().id, package.role(), messages);
        for (const std::string& message : messages) {
            Log::warning(LogCategory::Script, "NET: %s", message.c_str());
        }
    };
    for (const PackageRole role : {PackageRole::Engine, PackageRole::Base, PackageRole::Mod}) {
        for (const Package& package : assets.packages()) {
            if (package.role() == role) {
                loadOne(package);
            }
        }
    }
    schema.finalize();
    if (!schema.fields().empty() || !schema.events().empty()) {
        Log::info(
            LogCategory::Script,
            "NET: %zu declared fields, %zu events, schema %016llx",
            schema.fields().size(),
            schema.events().size(),
            static_cast<unsigned long long>(schema.hash()));
    }
}

}
