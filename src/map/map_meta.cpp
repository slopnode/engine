#include "map/map_meta.hpp"

#include "core/sexpr.hpp"

#include <string>

namespace slopengine {

namespace {

bool readStringField(const Sexpr& form, std::string& out) {
    if (!form.isList() || form.list.size() != 2 || !form.list[1].isString()) {
        return false;
    }
    out = form.list[1].text;
    return true;
}

bool readNumberField(const Sexpr& form, std::size_t count, float* out) {
    if (!form.isList() || form.list.size() != count + 1) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!form.list[i + 1].isNumber()) {
            return false;
        }
        out[i] = static_cast<float>(form.list[i + 1].number);
    }
    return true;
}

bool applySunField(const Sexpr& form, MapSun& sun) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        return false;
    }

    const std::string& tag = form.list[0].text;
    if (tag == "color") {
        float rgb[3] = {1.0f, 1.0f, 1.0f};
        if (!readNumberField(form, 3, rgb)) {
            return false;
        }
        sun.color = {rgb[0], rgb[1], rgb[2]};
        return true;
    }
    if (tag == "intensity") {
        float intensity = 1.0f;
        if (!readNumberField(form, 1, &intensity)) {
            return false;
        }
        sun.intensity = intensity;
        return true;
    }
    if (tag == "angles") {
        float angles[3] = {0.0f, 0.0f, 0.0f};
        if (!readNumberField(form, 3, angles)) {
            return false;
        }
        sun.angles = {angles[0], angles[1], angles[2]};
        return true;
    }
    if (tag == "yaw") {
        float yaw = 0.0f;
        if (!readNumberField(form, 1, &yaw)) {
            return false;
        }
        sun.angles = {0.0f, yaw, 0.0f};
        return true;
    }
    return false;
}

bool applySunForm(const Sexpr& form, MapSun& sun) {
    if (!form.isList() || form.list.empty() || !form.list[0].isAtom("sun")) {
        return false;
    }
    sun = {};
    sun.enabled = true;
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (!applySunField(form.list[i], sun)) {
            return false;
        }
    }
    return true;
}

bool applyMapField(const Sexpr& form, MapMeta& out) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        return false;
    }

    const std::string& tag = form.list[0].text;
    if (tag == "id") {
        return readStringField(form, out.id);
    }
    if (tag == "name") {
        return readStringField(form, out.name);
    }
    if (tag == "author") {
        return readStringField(form, out.author);
    }
    if (tag == "description") {
        return readStringField(form, out.description);
    }
    if (tag == "package") {
        std::string ignored;
        return readStringField(form, ignored);
    }
    if (tag == "depends") {
        out.depends.clear();
        for (std::size_t i = 1; i < form.list.size(); ++i) {
            if (!form.list[i].isString()) {
                return false;
            }
            out.depends.push_back(form.list[i].text);
        }
        return true;
    }
    if (tag == "ambient") {
        float rgb[3] = {0.02f, 0.02f, 0.025f};
        if (!readNumberField(form, 3, rgb)) {
            return false;
        }
        out.ambient = {rgb[0], rgb[1], rgb[2]};
        return true;
    }
    if (tag == "sun") {
        return applySunForm(form, out.sun);
    }
    return false;
}

} // namespace

bool parseMapMeta(std::string_view source, MapMeta& out) {
    out = {};
    const SexprParseResult parsed = parseSexprs(source);
    if (!parsed.ok) {
        return false;
    }
    if (parsed.forms.size() != 1 || !parsed.forms[0].isList() || parsed.forms[0].list.empty() ||
        !parsed.forms[0].list[0].isAtom("map")) {
        return false;
    }

    const Sexpr& root = parsed.forms[0];
    for (std::size_t i = 1; i < root.list.size(); ++i) {
        if (!applyMapField(root.list[i], out)) {
            return false;
        }
    }
    return !out.id.empty();
}

}
