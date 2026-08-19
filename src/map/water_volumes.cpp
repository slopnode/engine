#include "map/water_volumes.hpp"

namespace slopengine {

MapWaterVolumes buildMapWaterVolumes(const std::vector<Brush>& brushes) {
    MapWaterVolumes result;
    for (const Brush& brush : brushes) {
        if (brush.role != BrushRole::Water) {
            continue;
        }
        WaterVolume volume;
        volume.mins = brush.mins;
        volume.maxs = brush.maxs;
        volume.water = brush.water;
        for (const BrushFace& face : brush.faces) {
            if (face.normal.y > 0.7f) {
                continue;
            }
            volume.boundaryFaces.push_back(face);
        }
        result.volumes.push_back(std::move(volume));
    }
    return result;
}

}
