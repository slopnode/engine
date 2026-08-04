#include "test_assert.hpp"

#include "map/brush.hpp"
#include "map/csg_write.hpp"

#include <string>

namespace slopengine {

void runBrushBlocksTests() {
    CHECK_EQ(brushRoleDefaultBlocks(BrushRole::Hull), BrushBlock::All);
    CHECK_EQ(brushRoleDefaultBlocks(BrushRole::Hint), 0u);
    CHECK(brushRoleDefaultNocollide(BrushRole::Water));
    CHECK_FALSE(brushRoleDefaultNocollide(BrushRole::Hull));
    CHECK_FALSE(brushBlocksAny(0));
    CHECK(brushBlocksAny(BrushBlock::Player));

    {
        Brush brush = makeBrushBox("wall", {-1, 0, -1}, {1, 2, 1}, "mat/a", {});
        CHECK_EQ(brush.blocks, BrushBlock::All);
        CHECK_FALSE(brush.nocollide);

        setBrushBlocks(brush, static_cast<std::uint8_t>(BrushBlock::All & ~BrushBlock::Los));
        CHECK(brush.nocollide == false);
        CHECK((brush.blocks & BrushBlock::Los) == 0);
        CHECK((brush.blocks & BrushBlock::Player) != 0);

        setBrushBlocks(brush, 0);
        CHECK(brush.nocollide);
        CHECK_EQ(brush.blocks, 0u);
    }

    {
        Brush brush = makeBrushBox("open", {-1, 0, -1}, {1, 2, 1}, "mat/a", {}, BrushRole::Hint);
        CHECK_EQ(brush.blocks, 0u);
        setBrushBlocks(brush, BrushBlock::Player);
        CHECK((brush.blocks & BrushBlock::Player) != 0);
        CHECK_FALSE(brush.nocollide);

        const std::string text = brushesToCsgText({brush});
        CHECK(text.find("(block-player)") != std::string::npos);
    }

    {
        Brush brush = makeBrushBox("solid", {-1, 0, -1}, {1, 2, 1}, "mat/a", {});
        setBrushBlocks(brush, 0);
        const std::string text = brushesToCsgText({brush});
        CHECK(text.find("(nocollide)") != std::string::npos);
        CHECK(text.find("(no-block-") == std::string::npos);
    }

    {
        Brush brush = makeBrushBox("peek", {-1, 0, -1}, {1, 2, 1}, "mat/a", {});
        setBrushBlocks(brush, static_cast<std::uint8_t>(BrushBlock::All & ~BrushBlock::Los));
        const std::string text = brushesToCsgText({brush});
        CHECK(text.find("(no-block-los)") != std::string::npos);
        CHECK(text.find("(nocollide)") == std::string::npos);
    }
}

}
