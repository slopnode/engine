#include "map/water_volumes.hpp"

namespace slopengine {

MapWaterVolumes buildMapWaterVolumes(const std::vector<Brush>& brushes) {
    MapWaterVolumes result;
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Water) {
            continue;
        }
        result.volumes.push_back(WaterVolume{brush.mins, brush.maxs, brush.water});
    }
    return result;
}

}
