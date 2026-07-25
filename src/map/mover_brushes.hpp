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

bool faceIdBelongsToBrush(std::string_view faceId, std::string_view brushId);

bool faceIdBelongsToAnyMoverBrush(
    std::string_view faceId,
    const std::unordered_set<std::string>& brushIds);

void eraseFacFacesForMoverBrushes(FacFile& fac, const std::unordered_set<std::string>& brushIds);

const Brush* findBrushById(const std::vector<Brush>& brushes, std::string_view id);

}
