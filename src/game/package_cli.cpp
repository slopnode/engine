#include "game/package_cli.hpp"

#include <raylib.h>
#include <s7.h>

#include <cstring>
#include <string>

namespace slopengine {

namespace {

bool symbolEquals(s7_scheme* scheme, s7_pointer value, const char* name) {
    return s7_is_symbol(value) && std::strcmp(s7_symbol_name(value), name) == 0;
}

s7_pointer findSection(s7_scheme* scheme, s7_pointer catalog, const char* key) {
    for (s7_pointer cursor = catalog; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            TraceLog(LOG_WARNING, "CLI: *package-cli* entry is not a pair");
            continue;
        }
        if (symbolEquals(scheme, s7_car(entry), key)) {
            return s7_cdr(entry);
        }
    }
    return s7_nil(scheme);
}

s7_pointer propValue(s7_scheme* scheme, s7_pointer plist, const char* key) {
    for (s7_pointer cursor = plist; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry) || !symbolEquals(scheme, s7_car(entry), key)) {
            continue;
        }
        const s7_pointer rest = s7_cdr(entry);
        if (s7_is_pair(rest)) {
            return s7_car(rest);
        }
        return rest;
    }
    return s7_nil(scheme);
}

bool readStringProp(s7_scheme* scheme, s7_pointer plist, const char* key, std::string& out) {
    const s7_pointer value = propValue(scheme, plist, key);
    if (!s7_is_string(value)) {
        return false;
    }
    out = s7_string(value);
    return true;
}

} // namespace

std::vector<PackageCliFlag> parsePackageCliFromScheme(s7_scheme* scheme) {
    std::vector<PackageCliFlag> flags;
    if (scheme == nullptr) {
        return flags;
    }

    const s7_pointer catalog = s7_name_to_value(scheme, "*package-cli*");
    if (catalog == s7_undefined(scheme) || !s7_is_pair(catalog)) {
        TraceLog(LOG_WARNING, "CLI: *package-cli* missing or not a list");
        return flags;
    }

    const s7_pointer flagList = findSection(scheme, catalog, "flags");
    if (s7_is_null(scheme, flagList) && !s7_is_pair(flagList)) {
        TraceLog(LOG_WARNING, "CLI: *package-cli* has no flags entries");
        return flags;
    }

    for (s7_pointer cursor = flagList; s7_is_pair(cursor); cursor = s7_cdr(cursor)) {
        const s7_pointer entry = s7_car(cursor);
        if (!s7_is_pair(entry)) {
            TraceLog(LOG_WARNING, "CLI: skipping non-list flag entry");
            continue;
        }
        PackageCliFlag flag{};
        if (!readStringProp(scheme, entry, "name", flag.name) || flag.name.empty()) {
            TraceLog(LOG_WARNING, "CLI: flag entry missing string name");
            continue;
        }
        std::string valueKind = "string";
        if (!readStringProp(scheme, entry, "value", valueKind)) {
            TraceLog(LOG_WARNING, "CLI: flag '%s' missing value kind, defaulting to string", flag.name.c_str());
            valueKind = "string";
        }
        if (valueKind == "flag") {
            flag.kind = PackageCliValueKind::Flag;
        } else if (valueKind == "string") {
            flag.kind = PackageCliValueKind::String;
        } else {
            TraceLog(
                LOG_WARNING,
                "CLI: flag '%s' has unknown value kind '%s'",
                flag.name.c_str(),
                valueKind.c_str());
            continue;
        }
        readStringProp(scheme, entry, "help", flag.help);
        flags.push_back(std::move(flag));
    }

    return flags;
}

}
