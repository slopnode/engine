#include "map/map_meta.hpp"

#include "core/sexpr.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace slopengine {

namespace {

std::string escapeSexprString(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

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

bool applyRadField(const Sexpr& form, MapRadOptions& rad) {
    if (!form.isList() || form.list.empty() || form.list[0].kind != SexprKind::Atom) {
        return false;
    }

    const std::string& tag = form.list[0].text;
    if (tag == "luxels-per-meter") {
        return readNumberField(form, 1, &rad.luxelsPerMeter);
    }
    if (tag == "bounces") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.bounces = static_cast<int>(v);
        return true;
    }
    if (tag == "samples") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.samples = static_cast<int>(v);
        return true;
    }
    if (tag == "emitter-direct-samples") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.emitterDirectSamples = static_cast<int>(v);
        return true;
    }
    if (tag == "emitter-grid-luxels-per-meter") {
        return readNumberField(form, 1, &rad.emitterGridLuxelsPerMeter);
    }
    if (tag == "emitter-grid-max-size") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.emitterGridMaxSize = static_cast<int>(v);
        return true;
    }
    if (tag == "exact-emission-grid-max-size") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.exactEmissionGridMaxSize = static_cast<int>(v);
        return true;
    }
    if (tag == "exact-emission-max-samples") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.exactEmissionMaxSamples = static_cast<int>(v);
        return true;
    }
    if (tag == "sun-shadow-softness") {
        return readNumberField(form, 1, &rad.sunShadowSoftness);
    }
    if (tag == "seam-stitch-radius-luxels") {
        return readNumberField(form, 1, &rad.seamStitchRadiusLuxels);
    }
    if (tag == "probe-cell-size") {
        return readNumberField(form, 1, &rad.probeCellSize);
    }
    if (tag == "probe-fine-cell-size") {
        return readNumberField(form, 1, &rad.probeFineCellSize);
    }
    if (tag == "probe-sample-count") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.probeSampleCount = static_cast<int>(v);
        return true;
    }
    if (tag == "prefer-gpu") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.preferGpu = v != 0.0f;
        return true;
    }
    if (tag == "force-discrete-gpu") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.forceDiscreteGpu = v != 0.0f;
        return true;
    }
    if (tag == "gpu-safety-mode") {
        if (form.list.size() != 2 || form.list[1].kind != SexprKind::Atom) {
            return false;
        }
        const std::string& mode = form.list[1].text;
        if (mode == "auto") {
            rad.gpuSafetyMode = 0;
        } else if (mode == "fast") {
            rad.gpuSafetyMode = 1;
        } else if (mode == "safe") {
            rad.gpuSafetyMode = 2;
        } else {
            return false;
        }
        return true;
    }
    if (tag == "gpu-watchdog-limit-seconds") {
        return readNumberField(form, 1, &rad.gpuWatchdogLimitSeconds);
    }
    if (tag == "gpu-max-luxel-batch") {
        float v = 0.0f;
        if (!readNumberField(form, 1, &v)) {
            return false;
        }
        rad.gpuMaxLuxelBatch = static_cast<int>(v);
        return true;
    }
    return false;
}

bool applyRadForm(const Sexpr& form, MapMeta& out) {
    if (!form.isList() || form.list.empty() || !form.list[0].isAtom("rad")) {
        return false;
    }
    out.rad = {};
    out.hasRad = true;
    for (std::size_t i = 1; i < form.list.size(); ++i) {
        if (!applyRadField(form.list[i], out.rad)) {
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
    if (tag == "rad") {
        return applyRadForm(form, out);
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

std::string formatMapMeta(const MapMeta& meta) {
    std::ostringstream out;
    out << "(map\n";
    out << "  (id \"" << escapeSexprString(meta.id) << "\")\n";
    out << "  (name \"" << escapeSexprString(meta.name) << "\")\n";
    out << "  (author \"" << escapeSexprString(meta.author) << "\")\n";
    out << "  (description \"" << escapeSexprString(meta.description) << "\")\n";
    out << "  (depends";
    for (const std::string& depend : meta.depends) {
        out << " \"" << escapeSexprString(depend) << "\"";
    }
    out << ")\n";
    if (meta.ambient.x != 0.0f || meta.ambient.y != 0.0f || meta.ambient.z != 0.0f) {
        out << "  (ambient " << meta.ambient.x << " " << meta.ambient.y << " " << meta.ambient.z << ")\n";
    }
    if (meta.sun.enabled) {
        out << "  (sun\n";
        out << "    (color " << meta.sun.color.x << " " << meta.sun.color.y << " " << meta.sun.color.z
            << ")\n";
        out << "    (intensity " << meta.sun.intensity << ")\n";
        out << "    (angles " << meta.sun.angles.x << " " << meta.sun.angles.y << " " << meta.sun.angles.z
            << ")\n";
        out << "  )\n";
    }
    if (meta.hasRad) {
        const MapRadOptions& rad = meta.rad;
        const char* safetyMode = rad.gpuSafetyMode == 1 ? "fast" : rad.gpuSafetyMode == 2 ? "safe" : "auto";
        out << "  (rad\n";
        out << "    (luxels-per-meter " << rad.luxelsPerMeter << ")\n";
        out << "    (bounces " << rad.bounces << ")\n";
        out << "    (samples " << rad.samples << ")\n";
        out << "    (emitter-direct-samples " << rad.emitterDirectSamples << ")\n";
        out << "    (emitter-grid-luxels-per-meter " << rad.emitterGridLuxelsPerMeter << ")\n";
        out << "    (emitter-grid-max-size " << rad.emitterGridMaxSize << ")\n";
        out << "    (exact-emission-grid-max-size " << rad.exactEmissionGridMaxSize << ")\n";
        out << "    (exact-emission-max-samples " << rad.exactEmissionMaxSamples << ")\n";
        out << "    (sun-shadow-softness " << rad.sunShadowSoftness << ")\n";
        out << "    (seam-stitch-radius-luxels " << rad.seamStitchRadiusLuxels << ")\n";
        out << "    (probe-cell-size " << rad.probeCellSize << ")\n";
        out << "    (probe-fine-cell-size " << rad.probeFineCellSize << ")\n";
        out << "    (probe-sample-count " << rad.probeSampleCount << ")\n";
        out << "    (prefer-gpu " << (rad.preferGpu ? 1 : 0) << ")\n";
        out << "    (force-discrete-gpu " << (rad.forceDiscreteGpu ? 1 : 0) << ")\n";
        out << "    (gpu-safety-mode " << safetyMode << ")\n";
        out << "    (gpu-watchdog-limit-seconds " << rad.gpuWatchdogLimitSeconds << ")\n";
        out << "    (gpu-max-luxel-batch " << rad.gpuMaxLuxelBatch << ")\n";
        out << "  )\n";
    }
    out << ")\n";
    return out.str();
}

bool writeMapMeta(const std::filesystem::path& path, const MapMeta& meta) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << formatMapMeta(meta);
    return static_cast<bool>(file);
}

}
