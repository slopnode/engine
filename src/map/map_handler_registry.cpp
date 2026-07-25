#include "map/map_handler_registry.hpp"

#include <raylib.h>
#include <s7.h>

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

namespace slopengine {

namespace {

MapHandlerRegistry g_mapHandlerRegistry;

bool readStringValue(s7_scheme* scheme, s7_pointer value, std::string& out) {
    (void)scheme;
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

bool readAssoc(s7_scheme* scheme, s7_pointer alist, const char* key, s7_pointer& out) {
    if (!s7_is_pair(alist) && !s7_is_null(scheme, alist)) {
        return false;
    }
    const s7_pointer pair = s7_assoc(scheme, s7_make_symbol(scheme, key), alist);
    if (!s7_is_pair(pair)) {
        return false;
    }
    out = s7_cdr(pair);
    return true;
}

bool readAssocString(s7_scheme* scheme, s7_pointer alist, const char* key, std::string& out) {
    s7_pointer value = nullptr;
    if (!readAssoc(scheme, alist, key, value)) {
        return false;
    }
    return readStringValue(scheme, value, out);
}

bool parseDefaultValue(
    s7_scheme* scheme,
    HandlerArgType type,
    s7_pointer cursor,
    HandlerArgValue& out) {
    out = HandlerArgValue{};
    out.type = type;
    switch (type) {
    case HandlerArgType::Int:
        if (!s7_is_pair(cursor) || !s7_is_number(s7_car(cursor))) {
            return false;
        }
        out.i = static_cast<int>(s7_number_to_integer(scheme, s7_car(cursor)));
        return true;
    case HandlerArgType::Float:
        if (!s7_is_pair(cursor) || !s7_is_number(s7_car(cursor))) {
            return false;
        }
        out.f = static_cast<float>(s7_number_to_real(scheme, s7_car(cursor)));
        return true;
    case HandlerArgType::Bool:
        if (!s7_is_pair(cursor)) {
            return false;
        }
        if (s7_is_boolean(s7_car(cursor))) {
            out.b = s7_boolean(scheme, s7_car(cursor));
            return true;
        }
        return false;
    case HandlerArgType::String:
    case HandlerArgType::Thing:
    case HandlerArgType::Brush:
    case HandlerArgType::Face:
        if (!s7_is_pair(cursor)) {
            return false;
        }
        return readStringValue(scheme, s7_car(cursor), out.s);
    case HandlerArgType::Color:
    case HandlerArgType::Vec3:
        if (!s7_is_pair(cursor) || !s7_is_pair(s7_cdr(cursor)) ||
            !s7_is_pair(s7_cddr(cursor))) {
            return false;
        }
        if (!s7_is_number(s7_car(cursor)) || !s7_is_number(s7_cadr(cursor)) ||
            !s7_is_number(s7_caddr(cursor))) {
            return false;
        }
        out.v.x = static_cast<float>(s7_number_to_real(scheme, s7_car(cursor)));
        out.v.y = static_cast<float>(s7_number_to_real(scheme, s7_cadr(cursor)));
        out.v.z = static_cast<float>(s7_number_to_real(scheme, s7_caddr(cursor)));
        return true;
    }
    return false;
}

bool parseParam(s7_scheme* scheme, s7_pointer form, MapHandlerParam& out) {
    if (!s7_is_pair(form) || !s7_is_pair(s7_cdr(form))) {
        return false;
    }
    if (!readStringValue(scheme, s7_car(form), out.name) || out.name.empty()) {
        return false;
    }
    std::string typeName;
    if (!readStringValue(scheme, s7_cadr(form), typeName) ||
        !parseHandlerArgTypeName(typeName, out.type)) {
        return false;
    }
    const s7_pointer defaultCursor = s7_cddr(form);
    if (s7_is_pair(defaultCursor)) {
        if (!parseDefaultValue(scheme, out.type, defaultCursor, out.defaultValue)) {
            return false;
        }
        out.hasDefault = true;
    }
    return true;
}

} // namespace

MapHandlerRegistry& mapHandlerRegistry() {
    return g_mapHandlerRegistry;
}

bool parseMapHandlerKindName(std::string_view name, MapHandlerKind& out) {
    if (name == "use") {
        out = MapHandlerKind::Use;
        return true;
    }
    if (name == "enter") {
        out = MapHandlerKind::Enter;
        return true;
    }
    if (name == "exit") {
        out = MapHandlerKind::Exit;
        return true;
    }
    if (name == "touch") {
        out = MapHandlerKind::Touch;
        return true;
    }
    return false;
}

const char* mapHandlerKindName(MapHandlerKind kind) {
    switch (kind) {
    case MapHandlerKind::Use:
        return "use";
    case MapHandlerKind::Enter:
        return "enter";
    case MapHandlerKind::Exit:
        return "exit";
    case MapHandlerKind::Touch:
        return "touch";
    }
    return "use";
}

void MapHandlerRegistry::clear() {
    handlers_.clear();
}

bool MapHandlerRegistry::registerHandler(MapHandlerDef def) {
    if (def.id.empty()) {
        return false;
    }
    if (find(def.id) != nullptr) {
        TraceLog(LOG_WARNING, "MAPHANDLERS: duplicate id '%s' ignored", def.id.c_str());
        return false;
    }
    if (def.label.empty()) {
        def.label = def.id;
    }
    if (def.kinds.empty()) {
        TraceLog(LOG_WARNING, "MAPHANDLERS: '%s' has no kinds; ignored", def.id.c_str());
        return false;
    }
    handlers_.push_back(std::move(def));
    return true;
}

const MapHandlerDef* MapHandlerRegistry::find(std::string_view id) const {
    for (const MapHandlerDef& def : handlers_) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

std::vector<const MapHandlerDef*> MapHandlerRegistry::handlersForKind(MapHandlerKind kind) const {
    std::vector<const MapHandlerDef*> out;
    for (const MapHandlerDef& def : handlers_) {
        for (MapHandlerKind k : def.kinds) {
            if (k == kind) {
                out.push_back(&def);
                break;
            }
        }
    }
    return out;
}

bool MapHandlerRegistry::allowsKind(std::string_view id, MapHandlerKind kind) const {
    const MapHandlerDef* def = find(id);
    if (def == nullptr) {
        return true;
    }
    for (MapHandlerKind k : def->kinds) {
        if (k == kind) {
            return true;
        }
    }
    return false;
}

bool MapHandlerRegistry::mergeDefaults(HandlerBinding& binding) const {
    const MapHandlerDef* def = find(binding.id);
    if (def == nullptr) {
        return true;
    }
    for (const MapHandlerParam& param : def->params) {
        if (findHandlerArg(binding, param.name) != nullptr) {
            continue;
        }
        if (!param.hasDefault) {
            TraceLog(
                LOG_WARNING,
                "MAPHANDLERS: '%s' missing required param '%s'",
                binding.id.c_str(),
                param.name.c_str());
            return false;
        }
        binding.args.push_back(HandlerArg{param.name, param.defaultValue});
    }
    return true;
}

void MapHandlerRegistry::refineBinding(HandlerBinding& binding, MapHandlerKind kind) const {
    if (binding.empty()) {
        return;
    }
    const MapHandlerDef* def = find(binding.id);
    if (def == nullptr) {
        if (!binding.args.empty()) {
            TraceLog(
                LOG_WARNING,
                "MAPHANDLERS: '%s' has args but is not in catalog; args kept as legacy payload",
                binding.id.c_str());
        }
        return;
    }
    if (!allowsKind(binding.id, kind)) {
        TraceLog(
            LOG_WARNING,
            "MAPHANDLERS: '%s' is not valid for kind '%s'",
            binding.id.c_str(),
            mapHandlerKindName(kind));
    }
    for (HandlerArg& arg : binding.args) {
        bool found = false;
        for (const MapHandlerParam& param : def->params) {
            if (param.name != arg.name) {
                continue;
            }
            found = true;
            arg.value.type = param.type;
            break;
        }
        if (!found) {
            TraceLog(
                LOG_WARNING,
                "MAPHANDLERS: '%s' unknown param '%s'",
                binding.id.c_str(),
                arg.name.c_str());
        }
    }
}

bool registerPackageMapHandlersFromScheme(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return false;
    }

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-map-handlers*");
    if (s7_is_null(scheme, catalog)) {
        return true;
    }
    if (!s7_is_pair(catalog)) {
        return false;
    }

    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            continue;
        }

        MapHandlerDef def{};
        if (!readStringValue(scheme, s7_car(entry), def.id) || def.id.empty()) {
            continue;
        }

        const s7_pointer props = s7_cdr(entry);
        readAssocString(scheme, props, "label", def.label);

        s7_pointer kindsVal = nullptr;
        if (readAssoc(scheme, props, "kinds", kindsVal)) {
            for (s7_pointer k = kindsVal; s7_is_pair(k); k = s7_cdr(k)) {
                std::string kindName;
                MapHandlerKind kind{};
                if (readStringValue(scheme, s7_car(k), kindName) &&
                    parseMapHandlerKindName(kindName, kind)) {
                    def.kinds.push_back(kind);
                }
            }
        }

        s7_pointer paramsVal = nullptr;
        if (readAssoc(scheme, props, "params", paramsVal)) {
            for (s7_pointer p = paramsVal; s7_is_pair(p); p = s7_cdr(p)) {
                MapHandlerParam param{};
                if (parseParam(scheme, s7_car(p), param)) {
                    def.params.push_back(std::move(param));
                }
            }
        }

        mapHandlerRegistry().registerHandler(std::move(def));
    }

    bindMapHandlerArgClauses(scheme);
    return true;
}

bool isHandlerArgClauseName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) != 0 || first == '_' || first == '*')) {
        return false;
    }
    for (unsigned char c : name) {
        if (!(std::isalnum(c) != 0 || c == '-' || c == '_' || c == '!' || c == '?' || c == '*' ||
              c == '+')) {
            return false;
        }
    }
    return true;
}

void bindMapHandlerArgClauses(s7_scheme* scheme) {
    if (scheme == nullptr) {
        return;
    }

    const s7_pointer previousEnv = s7_set_curlet(scheme, s7_rootlet(scheme));
    std::unordered_set<std::string> seen;
    for (const MapHandlerDef& def : mapHandlerRegistry().handlers()) {
        for (const MapHandlerParam& param : def.params) {
            if (!isHandlerArgClauseName(param.name) || !seen.insert(param.name).second) {
                continue;
            }
            const std::string form =
                "(if (not (and (defined? '" + param.name + ") (procedure? " + param.name + "))) "
                "(define (" + param.name + " . args) (cons '" + param.name + " args)))";
            s7_eval_c_string(scheme, form.c_str());
        }
    }
    s7_set_curlet(scheme, previousEnv);
}

}
