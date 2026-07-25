#pragma once

#include "map/handler_binding.hpp"

#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

enum class MapHandlerKind {
    Use,
    Enter,
    Exit,
    Touch,
};

struct MapHandlerParam {
    std::string name;
    HandlerArgType type = HandlerArgType::String;
    bool hasDefault = false;
    HandlerArgValue defaultValue{};
};

struct MapHandlerDef {
    std::string id;
    std::string label;
    std::vector<MapHandlerKind> kinds;
    std::vector<MapHandlerParam> params;
};

class MapHandlerRegistry {
public:
    void clear();
    bool registerHandler(MapHandlerDef def);

    int size() const {
        return static_cast<int>(handlers_.size());
    }

    const MapHandlerDef* find(std::string_view id) const;
    std::vector<const MapHandlerDef*> handlersForKind(MapHandlerKind kind) const;
    const std::vector<MapHandlerDef>& handlers() const {
        return handlers_;
    }

    /** Merge catalog defaults into binding; returns false if required params missing. */
    bool mergeDefaults(HandlerBinding& binding) const;
    bool allowsKind(std::string_view id, MapHandlerKind kind) const;
    /** Apply catalog param types to parsed args; warn on unknown/extra params. */
    void refineBinding(HandlerBinding& binding, MapHandlerKind kind) const;

private:
    std::vector<MapHandlerDef> handlers_;
};

MapHandlerRegistry& mapHandlerRegistry();

bool parseMapHandlerKindName(std::string_view name, MapHandlerKind& out);
const char* mapHandlerKindName(MapHandlerKind kind);

/** Loads *package-map-handlers* from Scheme and registers them (append, dup ids ignored). */
bool registerPackageMapHandlersFromScheme(s7_scheme* scheme);

/** Bind catalog param names as tagged-list clauses for map CSG/things load. */
void bindMapHandlerArgClauses(s7_scheme* scheme);

}
