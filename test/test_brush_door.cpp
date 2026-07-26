#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/brush_door.hpp"
#include "map/mover_brushes.hpp"
#include "map/thing.hpp"
#include "physics/rigid_mover.hpp"

#include <cmath>

namespace slopengine {

namespace {

Brush makeDoorBrush(const char* id, DoorMotion motion) {
    Brush brush = makeBrushBox(
        id,
        {-1.0f, 0.0f, -0.06f},
        {1.0f, 2.2f, 0.06f},
        "mat/a",
        {},
        BrushRole::Door);
    brush.door.motion = motion;
    return brush;
}

} // namespace

void runBrushDoorTests() {
    {
        Brush brush = makeDoorBrush("door-raise", DoorMotion::Raise);
        RigidMover mover{};
        const Vector3 center{
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        configureBrushDoorMover(mover, brush, brush.door, center, nullptr);
        CHECK(mover.openPosOffset.y > 2.2f);
        CHECK_EQ(mover.openPosOffset.x, 0.0f);
        CHECK_EQ(mover.openPosOffset.z, 0.0f);
        CHECK(mover.pushMode == MoverPushMode::Horizontal);
        CHECK_FALSE(mover.slide);
    }

    {
        Brush brush = makeDoorBrush("door-slide", DoorMotion::Slide);
        RigidMover mover{};
        const Vector3 center{
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        configureBrushDoorMover(mover, brush, brush.door, center, nullptr);
        CHECK(std::fabs(mover.openPosOffset.x) > 1.0f);
        CHECK_EQ(mover.openPosOffset.y, 0.0f);
    }

    {
        Brush brush = makeDoorBrush("door-swing", DoorMotion::Swing);
        brush.door.haveAngle = true;
        brush.door.angle = static_cast<float>(M_PI) * 0.5f;
        brush.door.hingeThingId = "hinge-a";

        ThingDocument things{};
        Thing hinge{};
        hinge.id = "hinge-a";
        hinge.at = {-1.0f, 1.1f, 0.0f};
        hinge.haveAt = true;
        things.things.push_back(hinge);

        RigidMover mover{};
        const Vector3 center{
            0.5f * (brush.mins.x + brush.maxs.x),
            0.5f * (brush.mins.y + brush.maxs.y),
            0.5f * (brush.mins.z + brush.maxs.z),
        };
        configureBrushDoorMover(mover, brush, brush.door, center, &things);
        CHECK(mover.openAngleRadians > 1.5f);
        CHECK(mover.rotAxis == MoverRotAxis::Yaw);
        CHECK(std::fabs(mover.pivotLocal.x - (hinge.at.x - center.x)) < 1e-4f);

        RigidMover centered{};
        brush.door.hingeThingId.clear();
        configureBrushDoorMover(centered, brush, brush.door, center, &things);
        CHECK_EQ(centered.pivotLocal.x, 0.0f);
        CHECK_EQ(centered.pivotLocal.y, 0.0f);
        CHECK_EQ(centered.pivotLocal.z, 0.0f);
    }

    {
        Brush doorBrush = makeDoorBrush("door-1", DoorMotion::Raise);
        std::vector<Brush> brushes{doorBrush};

        ThingDocument doc{};
        Thing mover{};
        mover.kind = ThingKind::Mover;
        mover.id = "plat-1";
        mover.brush = "door-1";
        doc.things.push_back(mover);

        const auto claimed = collectClaimedBrushIds(&doc, brushes);
        CHECK(claimed.count("door-1") == 1);

        const auto doorsOnly = collectDoorBrushIds(brushes);
        CHECK(doorsOnly.count("door-1") == 1);

        const auto moversOnly = collectMoverBrushIds(doc);
        CHECK(moversOnly.count("door-1") == 1);
    }

    {
        Brush doorBrush = makeDoorBrush("door-2", DoorMotion::Raise);
        std::vector<Brush> brushes{doorBrush};
        const auto claimed = collectClaimedBrushIds(nullptr, brushes);
        CHECK(claimed.count("door-2") == 1);
    }

}

}
