#include "test_assert.hpp"

#include "map/brush.hpp"

#include <string>

namespace slopengine {

void runBrushRoleTests() {
    {
        BrushRole role = BrushRole::Hull;
        CHECK(parseBrushRoleName("transparent", role));
        CHECK(role == BrushRole::Transparent);
        CHECK(std::string(brushRoleName(BrushRole::Transparent)) == "transparent");
    }

    CHECK_FALSE(brushRoleOccludesVisFaces(BrushRole::Transparent));
    CHECK_FALSE(brushRoleReceivesVisOcclusion(BrushRole::Transparent));
    CHECK(brushRoleEmitsVisFaces(BrushRole::Transparent));
    CHECK(brushRoleSeals(BrushRole::Transparent));
    CHECK(brushRoleContributesSplits(BrushRole::Transparent));
    CHECK_FALSE(brushRoleNeedsInteriorPlacement(BrushRole::Transparent));

    CHECK(brushRoleOccludesVisFaces(BrushRole::Detail));
    CHECK(brushRoleReceivesVisOcclusion(BrushRole::Detail));
}

}
