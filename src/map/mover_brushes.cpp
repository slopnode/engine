#include "map/mover_brushes.hpp"

#include <raylib.h>

#include <cstddef>

namespace slopengine {

namespace {

std::string_view stripFacFragmentSuffix(std::string_view id) {
    const std::size_t hash = id.rfind('#');
    if (hash != std::string_view::npos) {
        return id.substr(0, hash);
    }
    return id;
}

std::string_view stripMergePrefix(std::string_view id) {
    constexpr std::string_view kMerge = "merge/";
    if (id.rfind(kMerge, 0) != 0) {
        return id;
    }
    const std::size_t slash = id.find('/', kMerge.size());
    if (slash == std::string_view::npos) {
        return id;
    }
    return id.substr(slash + 1);
}

bool partBelongsToBrush(std::string_view part, std::string_view brushId) {
    part = stripFacFragmentSuffix(part);
    if (part == brushId) {
        return true;
    }
    if (part.size() > brushId.size() + 1 && part.rfind(brushId, 0) == 0 &&
        part[brushId.size()] == '/') {
        return true;
    }
    return false;
}

} // namespace

std::unordered_set<std::string> collectMoverBrushIds(const ThingDocument& doc) {
    std::unordered_set<std::string> ids;
    for (const Thing& thing : doc.things) {
        if (thing.kind != ThingKind::Mover || thing.brush.empty()) {
            continue;
        }
        if (!ids.insert(thing.brush).second) {
            TraceLog(
                LOG_WARNING,
                "THING: duplicate mover brush claim '%s' (mover '%s')",
                thing.brush.c_str(),
                thing.id.c_str());
        }
    }
    return ids;
}

std::unordered_set<std::string> collectDoorBrushIds(const std::vector<Brush>& brushes) {
    std::unordered_set<std::string> ids;
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Door || brush.id.empty()) {
            continue;
        }
        ids.insert(brush.id);
    }
    return ids;
}

std::unordered_set<std::string> collectClaimedBrushIds(
    const ThingDocument* doc,
    const std::vector<Brush>& brushes) {
    std::unordered_set<std::string> ids;
    if (doc != nullptr) {
        ids = collectMoverBrushIds(*doc);
    }
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Door || brush.id.empty()) {
            continue;
        }
        if (!ids.insert(brush.id).second) {
            TraceLog(
                LOG_ERROR,
                "MAP: door brush '%s' is also claimed by a mover",
                brush.id.c_str());
        }
    }
    return ids;
}

bool faceIdBelongsToBrush(std::string_view faceId, std::string_view brushId) {
    if (brushId.empty() || faceId.empty()) {
        return false;
    }
    std::string_view remaining = stripMergePrefix(faceId);
    while (!remaining.empty()) {
        const std::size_t plus = remaining.find('+');
        const std::string_view part =
            plus == std::string_view::npos ? remaining : remaining.substr(0, plus);
        if (partBelongsToBrush(part, brushId)) {
            return true;
        }
        if (plus == std::string_view::npos) {
            break;
        }
        remaining = remaining.substr(plus + 1);
    }
    return false;
}

bool faceIdBelongsToAnyMoverBrush(
    std::string_view faceId,
    const std::unordered_set<std::string>& brushIds) {
    for (const std::string& brushId : brushIds) {
        if (faceIdBelongsToBrush(faceId, brushId)) {
            return true;
        }
    }
    return false;
}

void eraseFacFacesForMoverBrushes(FacFile& fac, const std::unordered_set<std::string>& brushIds) {
    if (brushIds.empty()) {
        return;
    }
    std::vector<VisibleFace> kept;
    kept.reserve(fac.faces.size());
    for (VisibleFace& face : fac.faces) {
        const std::string_view source =
            face.sourceFaceId.empty() ? std::string_view(face.id) : std::string_view(face.sourceFaceId);
        if (faceIdBelongsToAnyMoverBrush(source, brushIds) ||
            faceIdBelongsToAnyMoverBrush(face.id, brushIds)) {
            continue;
        }
        kept.push_back(std::move(face));
    }
    fac.faces = std::move(kept);
}

const Brush* findBrushById(const std::vector<Brush>& brushes, std::string_view id) {
    for (const Brush& brush : brushes) {
        if (brush.id == id) {
            return &brush;
        }
    }
    return nullptr;
}

}
