#pragma once

#include "map/brush.hpp"
#include "map/fac.hpp"
#include "map/thing.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace slopengine {

std::unordered_set<std::string> collectMoverBrushIds(const ThingDocument& doc);
std::unordered_set<std::string> collectDoorBrushIds(const std::vector<Brush>& brushes);
/** Mover claims plus door brushes; logs conflicts when both claim the same id. */
std::unordered_set<std::string> collectClaimedBrushIds(
    const ThingDocument* doc,
    const std::vector<Brush>& brushes);

bool faceIdBelongsToBrush(std::string_view faceId, std::string_view brushId);

bool faceIdBelongsToAnyMoverBrush(
    std::string_view faceId,
    const std::unordered_set<std::string>& brushIds);

void eraseFacFacesForMoverBrushes(FacFile& fac, const std::unordered_set<std::string>& brushIds);

/** Faces belonging to any of @p brushIds (inverse of eraseFacFacesForMoverBrushes). */
FacFile extractFacFacesForMoverBrushes(const FacFile& fac, const std::unordered_set<std::string>& brushIds);

const Brush* findBrushById(const std::vector<Brush>& brushes, std::string_view id);

}
