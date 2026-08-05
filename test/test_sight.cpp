#include "test_assert.hpp"

#include "physics/sight_math.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace slopengine {

void runSightTests() {
    {
        const std::vector<std::string> target{"player", "team:marine"};
        CHECK(sightTagsAllow({}, {}, target));
        CHECK(sightTagsAllow({"player"}, {}, target));
        CHECK(sightTagsAllow({"team:marine"}, {}, target));
        CHECK_FALSE(sightTagsAllow({"team:hell"}, {}, target));
        CHECK_FALSE(sightTagsAllow({"player"}, {"player"}, target));
        CHECK_FALSE(sightTagsAllow({}, {"team:marine"}, target));
        CHECK(sightTagsAllow({"player"}, {"team:hell"}, target));
    }

    {
        CHECK(sightInFov(0.0f, 0.0f, 1.0f, 90.0f));
        CHECK(sightInFov(0.0f, 0.0f, 1.0f, 360.0f));
        CHECK_FALSE(sightInFov(0.0f, 0.0f, -1.0f, 90.0f));
        CHECK(sightInFov(0.0f, 0.0f, -1.0f, 360.0f));
        CHECK_FALSE(sightInFov(0.0f, 1.0f, 0.0f, 90.0f));
        CHECK(sightInFov(0.0f, 1.0f, 1.0f, 120.0f));
        CHECK_FALSE(sightInFov(0.0f, 1.0f, 0.0f, 0.0f));
        const float yaw = 3.14159265358979323846f * 0.5f;
        CHECK(sightInFov(yaw, 1.0f, 0.0f, 40.0f));
        CHECK_FALSE(sightInFov(yaw, 0.0f, 1.0f, 40.0f));
    }

    {
        CHECK(std::fabs(sightEyeOffset(1.0f, 0.3f, 0.75f) - 1.05f) < 1e-5f);
        CHECK(std::fabs(sightAngleDeltaRad(0.0f, 3.14159265358979323846f) - 3.14159265358979323846f) <
              1e-5f);
        CHECK(std::fabs(sightAngleDeltaRad(3.0f, -3.0f)) < 1.0f);
    }
}

}
